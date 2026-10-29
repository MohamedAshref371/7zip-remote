# الأدلة وحدود الدراسة والأسئلة المتبقية

**تنبيه زمني:** هذا الملف يسجل أدلة الدراسة الأولى. بعد طلب بدء التنفيذ أُنجز النموذج والاختبارات؛ الحالة الأحدث في [نتائج التنفيذ الأول](06-prototype-results.md).

## ما تم فعله

- فحص بنية المستودع وحالة Git؛ كانت شجرة العمل نظيفة عند بداية الدراسة، وكان `Notes` فارغًا.
- التحقق من النسخة في [C/7zVersion.h](../C/7zVersion.h) وقراءة تعليمات البناء المحلية.
- قراءة عقود streams والأرشيف، وتتبع نقاط الفتح والاستخراج المحلية في Common وConsole وAgent وFile Manager.
- فحص مسارات ZIP و7z وRAR/RAR5 وTAR وGZIP المرتبطة بالـSeek واختيار العناصر.
- فحص callbacks الخاصة بالأجزاء المتعددة وإعادة الفتح وخصائص القراءة فقط وZone metadata.
- البحث في `CPP` و`C` عن `winhttp` و`wininet` و`curl_easy` و`InternetOpen` و`HttpSendRequest` و`URLDownload`؛ لم تظهر مطابقة. هذا دليل بحث محدد، وليس برهانًا على غياب كل كود شبكة محتمل بأي تسمية.
- مراجعة مراجع HTTP وWinHTTP وlibcurl الرسمية لتثبيت صلاحية تصميم طبقة النقل.

لم أقرأ كل ملفات المشروع سطرًا بسطر. الدراسة مركزة على الميزة المطلوبة ومساراتها الأساسية. لم أستخدم reverse engineering لأن المصدر الأصلي متاح والمطلوب تحليل معماري له.

## درجة الثقة

| الاستنتاج | نوع الدليل | درجة الثقة/الحد |
|---|---|---|
| يمكن حقن مصدر غير ملف محلي | `IInArchive::Open` و`COpenOptions.stream` و`CAgent::Open` | مرتفعة من المصدر |
| CLI يحتاج تغييرات قبل مرحلة Open | enumeration و`CFileInfo` في Main/List/Extract | مرتفعة من المصدر |
| ReOpen الحالي لا يحافظ على stream خارجي | تصفير stream وإنشاء `CInFileStream` | مرتفعة من المصدر |
| ZIP مناسب للقراءة الجزئية | tail/central-directory/item-stream في handler | مرتفعة معماريًا؛ حجم التوفير يحتاج قياسًا |
| Solid قد يوسع البيانات اللازمة لملف مختار | خرائط Folder ومسار فك الضغط السابق للعنصر | مرتفعة من المصدر؛ التكلفة العددية غير مقاسة |
| WinHTTP مناسب كبداية Windows | وثائق API وتوافقه مع نطاق رابط مباشر | اختيار هندسي، يحتاج بناء وتجربة |
| يمكن تجنب تعديل parsers غالبًا | فصل streams عن handlers | استنتاج معماري قوي؛ لم يُثبت بكل الصيغ |
| الميزة جاهزة للإصدار | لم يكن هناك تنفيذ عند الدراسة | **غير صحيح**؛ النموذج اللاحق أيضًا لم يندمج في المنتج بعد |

## ما لم يكن قد تم وقت الدراسة الأولى

لم يتم بناء 7-Zip، أو إضافة backend، أو تعديل كود التشغيل، أو تنزيل أرشيف تجريبي، أو قياس استهلاك الشبكة، أو اختبار TLS/proxy فعليًا. خطة الاختبارات في الملف السابق متطلبات مستقبلية، وليست اختبارات ناجحة بالفعل.

لم يتم تحديد صلاحية مكتبة أو dependency جديدة قانونيًا. عند التنفيذ والتوزيع تراجع تراخيص المكونات مع ملفات المشروع [License.txt](../DOC/License.txt)؛ لا تضيف الدراسة dependency فعلية.

## قرارات يمكن حسمها عند بدء التنفيذ

| السؤال | الافتراض المستخدم في الخطة | متى يحتاج الحسم؟ |
|---|---|---|
| هل الهدف CLI أم File Manager؟ | الإثبات ثم CLI ثم File Manager | قبل تحديد نطاق أول إصدار؛ File Manager مطلوب لتحقيق التصفح الرسومي |
| Windows فقط أم منصات متعددة؟ | Windows أولًا مع واجهة نقل قابلة للاستبدال | قبل تثبيت backend وbuild matrix |
| هل يلزم التعامل مع روابط login؟ | روابط مباشرة عامة أو signed URLs، دون جلسة متصفح | قبل تصميم auth UI |
| ماذا نفعل دون Range/validator؟ | fallback كامل بسياسة ظاهرة أو خطأ | قبل تثبيت UX وخيارات CLI |
| ما حدود RAM/cache/download؟ | قيم تجريبية قابلة للضبط | بعد traces وقياس fixtures |
| هل تقبل links بلا strong ETag للتنزيل الجزئي؟ | لا افتراضيًا؛ snapshot كامل | قبل توسيع التوافق أو إضافة object-version policy |
| هل يلزم split archives؟ | مؤجل مع resolver صريح | بعد إثبات الأرشيف المفرد |
| ما حد Windows/SDK المدعوم؟ | لم يحدد؛ عدم كسر البناء المحلي القديم دون قرار | قبل اختيار APIs وتعديل makefiles |

هذه ليست أسئلة منعت اكتمال الدراسة. طلب المستخدم اللاحق «ابدأ» أذن ببدء التنفيذ؛ استُخدمت الافتراضات المذكورة للنموذج الأول دون تحميل أرشيفات عامة كبيرة.

## مراجع خارجية

تمت مراجعتها في 29 أكتوبر 2026. تفاصيل استخدامها موثقة بجوار القرارات في [تصميم المصدر الشبكي](03-http-stream-design.md).

- [RFC 9110: Range requests](https://www.rfc-editor.org/rfc/rfc9110.html#section-14).
- [RFC 9110: If-Match](https://www.rfc-editor.org/rfc/rfc9110.html#section-13.1.1).
- [Microsoft: About WinHTTP](https://learn.microsoft.com/en-us/windows/win32/winhttp/about-winhttp).
- [Microsoft: WinHttpQueryHeaders](https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpqueryheaders).
- [Microsoft: WinHttpSetTimeouts](https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpsettimeouts).
- [libcurl: CURLOPT_RANGE](https://curl.se/libcurl/c/CURLOPT_RANGE.html).

## الخطوة التقنية التالية الأكثر فائدة

تنفيذ tracing stream ثم HTTP stream صغير وتمريره إلى محرك ZIP و7z الموجود، مع خادم اختبارات محلي. نجاح هذا الإثبات يحول الاستنتاج المعماري إلى دليل تنفيذي، ويعطي أرقامًا حقيقية لاختيار cache قبل صرف جهد تكامل الواجهة.
