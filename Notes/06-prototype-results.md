# التنفيذ الأول: مصدر HTTP وأداة تتبّع واستخراج

تاريخ التنفيذ: 29 أكتوبر 2026. هذا تحديث للدراسة بعد طلب «ابدأ».

## ما أصبح موجودًا

أداة مستقلة `7zremote.exe` تعرض عناصر ZIP و7z وتستخرج عنصرًا محددًا أو تختبره، من ملف محلي أو رابط مباشر. تستخدم محرك 7-Zip من DLL، دون إعادة تنفيذ parsers أو خوارزميات فك الضغط.

| الملف | الوظيفة المنفذة |
|---|---|
| [HttpRangeSource.cpp](../CPP/Windows/HttpRangeSource.cpp) | WinHTTP وranges والتحقق من headers وETag والحجم والأخطاء والمهل |
| [HttpInStream.cpp](../CPP/7zip/Common/HttpInStream.cpp) | `IInStream` و`IStreamGetSize`، مؤشر 64-bit وcache بحد 8 MiB |
| [TracingInStream.cpp](../CPP/7zip/Common/TracingInStream.cpp) | تسجيل قراءات وانتقالات المحرك مع فصل مرحلتي الفتح والاستخراج |
| [Remote7z.cpp](../CPP/7zip/UI/Remote7z/Remote7z.cpp) | برنامج تجريبي، listing بصيغة JSON، واختيار عنصر بالـindex |
| [build.py](../Tests/RemoteArchive/build.py) | بناء Windows x64 باستخدام compiler محمول داخل المشروع |
| [test_remote.py](../Tests/RemoteArchive/test_remote.py) | fixtures وخادم HTTP محلي واختبارات تكامل |

استخدمت cache داخل `CHttpInStream` وطبقة WinHTTP مستقلة، بدل إنشاء كل abstraction مقترح مقدمًا. `CArchiveSource` وresolver للأجزاء ودمج الواجهات لم تُنفذ بعد.

## البيئة وحدود الإثبات

- واجهات المشروع والـstreams المحلي من مستودع **26.05**.
- محرك التشغيل: `C:\Program Files\7-Zip\7z.dll`، إصدار **26.02** المثبت.
- SHA-256 للمحرك: `69fd4df057985c40e510e2fac182881c7f85e90aa13ec703f763a8fdb2ce61f8`.
- البناء: Zig 0.13.0 / Clang، Windows x64، C++17، مع `-Wall -Wextra -Werror` لكود التطبيق. ظهرت تحذيرات في runtime الخاص بالـtoolchain في البناء الأول.
- Visual Studio موجود لكن C++ toolset غير متاح؛ نُزّل compiler إلى `.build/tools` دون تثبيت نظامي. قيود sandbox على compiler cache وloopback استلزمت صلاحية تنفيذ مناسبة للبناء والاختبارات.
- لم يُبنَ محرك 26.05 كاملًا، ولم يُستبدل DLL أو `7z.exe` المثبت، ولم يتغير File Manager.

النتيجة تثبت عمل stream الجديد مع محرك وواجهات 7-Zip حقيقية، وليست شهادة توافق نهائية مع build كامل من 26.05.

## النتائج المقاسة

**نجح التشغيل النهائي في 72 سيناريو.** الحالات الفاشلة عمدًا نجح التحقق من رفضها؛ العدد لا يعني 72 أرشيفًا مختلفًا.

الملف الصغير المختار `002-target.txt` حجمه **4800 بايت**، وقبله نحو 4 MiB من بيانات شبه عشوائية. كل تشغيل يبدأ cache جديدة، والقياس يشمل فتح الأرشيف ثم استخراج العنصر:

| الأرشيف | حجمه بالبايت | HTTP body المقروء بالبايت | الطلبات | الملاحظة |
|---|---:|---:|---:|---|
| ZIP مع تعليق طويل | 6,359,571 | 854,548 | 5 | نحو 13.4% من الأرشيف |
| 7z غير Solid | 6,292,169 | 525,002 | 4 | نحو 8.3% من الأرشيف |
| 7z Solid | 6,292,823 | 4,719,960 | 20 | نحو 75%؛ يحتاج فك البيانات السابقة |
| 7z برأس مشفر | 6,292,337 | 525,170 | 4 | نجح بكلمة المرور الصحيحة |
| ZIP بتشفير AES | 6,292,316 | 787,293 | 5 | نجح بكلمة المرور الصحيحة |

تطابقت SHA-256 للمخرجات المحلية والشبكية مع الملفات الأصلية، وتطابقت قوائم العناصر. شملت الاختبارات ملفات في البداية والوسط والنهاية، وأسماء Unicode، واختبار عناصر دون إخراج.

في trace المحلي للعنصر نفسه: قارئ 7z غير Solid قرأ **84 بايتًا** في مرحلة الاستخراج، بينما Solid قرأ **4,456,448 بايتًا**. كلاهما قرأ 230 بايتًا في الفتح في هذه العينة. الفارق بين احتياج المحرك وحجم HTTP يوضح كلفة prefetch؛ blocks مقدارها 256 KiB قابلة للتحسين.

اختبار `streamcheck` تعامل مع مصدر افتراضي حجمه **4,296,015,872 بايتًا**، وقرأ **1,048,577 بايتًا** عبر 5 طلبات، مع فحص بيانات بعد حاجز 4 GiB وحدود blocks وcache hits وEOF والـoverflow.

هذه قياسات loopback وfixtures محددة، وليست benchmark لسرعة الإنترنت أو ضمانًا للتوفير. العداد يقيس body bytes التي أعادها WinHTTP للتطبيق، دون headers/TLS أو buffering داخلي غير مسلّم للتطبيق.

## الحالات التي تم التحقق منها

- Range صحيح دون `Accept-Ranges`، وردود chunked صحيحة، وحفظ signed query بحروفه وترتيبه.
- تجاهل Range وإرجاع 200؛ غياب/تكرار/فساد Content-Range وETag؛ weak ETag.
- تبدل ETag أو الحجم، وHTTP 412؛ يتوقف stream دون مزج نسختين.
- body ناقص أو زائد، Content-Length خاطئ، content encoding غير مدعوم، ورد multipart.
- رفض redirects في هذا النموذج، و403 و404 و416.
- كلمة مرور مفقودة أو خاطئة؛ تنظيف الإخراج الجديد عند الفشل؛ عدم الكتابة فوق ملف موجود.
- إلغاء قبل أول طلب وأثناء انتظار الرد، واختبار مهلة HTTP.
- `Read(0)`، وقراءات قصيرة عند حدود cache، وSeek سالب وغير صالح وبعد النهاية، وحسابات 64-bit.

## ملاحظات ظهرت أثناء التنفيذ

1. اختبار المهلة الأول كشف أن إعداد session وحده لم يحقق توقع timeout صغير؛ أضيف الضبط على request و`WINHTTP_OPTION_RECEIVE_RESPONSE_TIMEOUT`. ظل السلوك غير فوري: timeout رجع بعد نحو **4.1 ثانية** رغم إعداد 200 ms، والإلغاء أثناء انتظار رد متأخر استغرق نحو **0.9 ثانية**. لا نعلن إلغاء فوريًا أو deadline صارمًا بالآلية المتزامنة؛ تصميم async محكم مطلوب للواجهة النهائية. [مرجع WinHTTP](https://learn.microsoft.com/en-us/windows/win32/winhttp/option-flags#winhttp_option_receive_response_timeout).
2. HTTP 200 يرفض عند headers ولا يوضع عند offset جزئي. لا يوجد تنزيل كامل تلقائي.
3. strong ETag شرط صارم، بما فيه رده مع الطلبات اللاحقة؛ قد يرفض خوادم يمكن دعمها لاحقًا.
4. cache ثابتة 8 MiB وblocks مقدارها 256 KiB. العينات لا تثبت الأداء تحت eviction كثيف أو RTT مرتفع.
5. استخراج عنصر واحد لمسار صريح مع CREATE_NEW لا ينفذ timestamps والروابط وADS وZone.Identifier، ولا يشغّل المخرجات. هذه أداة إثبات وليست بديلًا كاملًا لمسار استخراج المنتج.

## التشغيل

التنفيذي: `.build/remote/7zremote.exe`.

```powershell
python Tests/RemoteArchive/build.py
python Tests/RemoteArchive/test_remote.py

& .\.build\remote\7zremote.exe l zip "https://example.com/archive.zip" --dll "C:\Program Files\7-Zip\7z.dll"
& .\.build\remote\7zremote.exe x zip "https://example.com/archive.zip" --dll "C:\Program Files\7-Zip\7z.dll" --index 2 --out ".\result.bin"
```

الرابط مثال يجب استبداله برابط مباشر يدعم المتطلبات. [دليل الأداة](../Tests/RemoteArchive/README.md) يشرح البناء وكلمة المرور والـtrace والقيود.

التقرير الكامل وCSV traces داخل `.build/remote/runs/run-baybstum/`، وهي artifacts متجاهلة في Git. ملخص محفوظ في المستودع: [remote-prototype-results.json](remote-prototype-results.json).

## ما تبقى قبل الدمج

التالي بناء واختبار محرك 26.05 نفسه، ثم دمج source descriptor في CLI ومسارات metadata وOpen/ReOpen، ثم واجهة «فتح رابط» في File Manager.

لم تُختبر اتصالات HTTPS ناجحة أو proxy فعلي أو ZIP64 كأرشيف أو SFX أو الأجزاء المتعددة. اختبار المصدر فوق 4 GiB يخص stream وليس ZIP64 parser. TLS validation بافتراضات Windows دون flags لتجاوز الشهادات. redirects وretries وfallback والتوازي وcache القرص ليست منفذة.
