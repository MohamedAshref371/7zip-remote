# Partial Reading: What Is Actually Possible?

These are conclusions from the code, not measurements. Support for a format in 7-Zip does not automatically make it economical over a network.

## Format Comparison

| Format | Listing contents | Extracting a selected file | Initial assessment |
|---|---|---|---|
| Valid, single-volume ZIP / ZIP64 | Read the archive tail, then the Central Directory, with additional checks | Read the local header and required item data | Best initial candidate |
| Non-solid 7z | Read the beginning and Next Header, sometimes including compressed-header streams | Read streams belonging to the required compression unit | Very good candidate |
| Solid 7z | The index can remain relatively small | Decompress from the start of the compression unit through the requested items; buffering and coding method may cause additional reads | Logically supported; savings are not guaranteed |
| Non-solid RAR/RAR5 | Walk headers distributed through the archive, skipping data | Read selected item data while handling format relationships | Possible, but latency may be high |
| Solid RAR/RAR5 | As above; encryption may change opening costs | Return to the beginning of the relevant compression group | Needs clear expectations and measurement |
| Uncompressed TAR | Walk headers and skip payload using Seek | Read data positions after building the list | Can save bytes, but many files mean many requests |
| GZIP / TAR.GZ | GZIP itself is a stream; listing the TAR inside is another stage | Late access usually requires decompressing preceding data | No promise of economical extraction from arbitrary offsets |
| Archive inside an archive | Depends on whether the inner item exposes `IInStream` | May require materializing the inner item in a temporary file | Later phase |
| Multi-volume archive | Requires discovering and connecting all required sources | An item may depend on several volumes | Outside the MVP |
| ISO/UDF/XZ and other formats | Their behavior has not been studied in detail here | Compatibility requires separate tests | Do not claim support before verification |

## ZIP: A Clear Example of Reusing the Existing Engine

In [ZipIn.cpp](../CPP/7zip/Archive/Zip/ZipIn.cpp), `FindCd` at 1722:

1. Obtains the source end using Seek.
2. Reads up to `1 << 17`, or 128 KiB, from the tail to find directory-end records.
3. `TryReadCd` around 1868 seeks to and reads the Central Directory.
4. [ZipHandler.cpp](../CPP/7zip/Archive/Zip/ZipHandler.cpp), `Extract` at 1561, processes the requested items; item decompression uses `GetItemStream` around 1046.

This directly shows that reading does not require downloading the entire file first. However, 128 KiB applies to that search step, not the total listing cost. The directory may be huge, and SFX or damaged data may send the engine through other paths.

## 7z: The Index Is Separate from Solid Compression

In [7zIn.cpp](../CPP/7zip/Archive/7z/7zIn.cpp), `ReadDatabase2` at 1633 obtains `nextHeaderOffset` and `nextHeaderSize` from the header, then seeks to and reads the Next Header around 1730. The structure is also described in [7zFormat.txt](../DOC/7zFormat.txt).

The header may be compressed or encrypted; encryption requires a password to list names. We reuse existing callbacks rather than reimplementing encryption.

In [7zExtract.cpp](../CPP/7zip/Archive/7z/7zExtract.cpp), around 260 and 317, the engine uses `FileIndexToFolderIndexMap` and `FolderStartFileIndex`. Here, Folder means a **compression unit/chain**, not necessarily a directory in the filename tree. The engine identifies the unit start and accumulates the decompressed size through the last requested item in it.

Therefore:

- A small file late in a large solid block cannot necessarily be decompressed independently.
- It is incorrect to assume every solid archive always requires a full download; independent units may exist, and part of a unit may suffice depending on the file and method.
- Packed progress must not be used as a network counter; the code may count the entire unit size while actual reads differ.
- When users control archive creation, placing files in independent units or reducing solid block sizes can improve remote access at the expense of compression ratio. This is an archive configuration choice, not a fix a reader can impose on an existing archive.

## RAR and TAR

In [RarHandler.cpp](../CPP/7zip/Archive/Rar/RarHandler.cpp), around 727, and [Rar5Handler.cpp](../CPP/7zip/Archive/Rar/Rar5Handler.cpp), around 2423, the engine skips compressed data to reach a later header. During RAR5 extraction around 2840 it returns to earlier items according to solid relationships.

In [TarHandler.cpp](../CPP/7zip/Archive/Tar/TarHandler.cpp), around 233 and 254, the engine reads a header and skips the aligned item size. Extraction around 838 seeks to the data position. This can avoid unwanted payload, but does not necessarily eliminate requests for scattered headers.

In [GzHandler.cpp](../CPP/7zip/Archive/GzHandler.cpp), `Open` at 616 and `Extract` at 672 handle the beginning and end of a stream; this does not provide an index to every file in TAR.GZ. Do not generalize to XZ or formats with different indexes and blocks without studying them.

## A Performance Model for Reasoning and Measurement

A simplified approximation for sequential requests:

```text
Time ≈ requests not served from cache × RTT
       + downloaded bytes ÷ transfer rate
       + decompression and disk cost
```

These costs may overlap, so the equation is not a benchmark. For example, 1000 sequential requests with an RTT of 80 ms imply about 80 seconds of latency alone; downloading a wider range may be faster.

Useful metrics include time to first listing, file extraction time, request count, network payload including retries, unique range bytes, hit ratio, peak RAM, cache disk usage, and total archive size. Do not promise a savings percentage before measuring.

Testing all archive contents (`t`) usually requires reading all compressed data involved in the test. Listing is not a comprehensive archive integrity check.
