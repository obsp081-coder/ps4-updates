## تحديث لاحق 10 نوفمبر 2026 — SHA-256 Native فعلي على الملفات المخزنة

**Verdict: MODIFY — نجاح Native C SHA-256 + Host + OpenOrbis؛ PS4 Hardware وDownloader الفعلي ما زالا غير مثبتين.**

- أضفنا `ps4/src/download_sha256.h/.c` بمحرك SHA-256 مبني على قواعد `FIPS 180-4`، قادر على العمل تدريجيًا بكتل صغيرة، وبحد منطقي لعدد البايتات وإعادة استخدام السياق بعد Final. لا يعتمد على الإنترنت أو مكتبات Crypto إضافية.
- `ds_sha256_verify_stored()` يعيد فتح ملف `.part` عبر `DsSha256Reader` من Backend مقيّد، ويقرأ كل المحتوى ليتحقق من **حجم مطابق تمامًا** و**بصمة SHA-256 مطابقة** مع إغلاق ناجح. لا يقرأ البصمة من `Content-Length` أو يثق بمحتوى غير موثق. `ds_finalize()` يمرّر `expected_size` إلى `verify_sha256` قبل `publish_exclusive`.
- استبدلنا فحص Fixture القديم الذي يعتمد على Token تجريبي، بتجزئة **الملف المخزن فعليًا** في `tests/ui_mocks/storage_smoke.c`. مرجع Digest مستقل محسوب باستخدام Python `hashlib` لملف اختبار حجمه 41 بايت.
- رفضت اختبارات Host حالة تغيير بايت واحد في الملف، والقطع، وإضافة بايت زائد، وتقديم Digest خاطئة، وفشل القراءة، ومحاولات نشر ملف غير معتمد.
- اختبارات خوارزمية SHA-256 تشمل Empty/abc/55/56/64/65/256 Byte ومليون بايت، وتعدد دفعات `update()` وحماية Overflow، باستخدام Test Vectors مستقلة. توجد في `tests/ui_mocks/sha256_smoke.c` و`tests/test_download_sha256.py`.
- **Code SHA:** `0d3392fad23499a4b25c72b40bf6e269eaa41d26`.
- **Host CI:** **214/214 PASS** — https://github.com/obsp081-coder/ps4-updates/actions/runs/42794816996
- **OpenOrbis Package Build:** **PASS** — https://github.com/obsp081-coder/ps4-updates/actions/runs/42794809447
- كشف بناء OpenOrbis الأول خطأ في Makefile (`clang: no input files`)؛ أُصلح بإضافة مصدر `$<` للأمر، ثم أُعيد CI حتى النجاح. لا يوجد Release نهائي جديد.

**حدود الدليل:** يعمل حساب SHA-256 فعليًا على Host، ونجح Compile/Link على OpenOrbis؛ **لم تُثبت سرعة التجزئة أو صلاحيات القراءة/الكتابة على FW14**. Backend التخزين الوحيد الفعلي في الاختبارات هو POSIX Host، وليس PS4. `SHA-256` يثبت التطابق مع بصمة مرجعية **إذا كانت مرجعية مستقلة ومصرحًا بها**، ولا يثبت وحده الحقوق أو توقيع Retail/FPKG أو التوافق. التطبيق الفعلي يبقى **Read Only**، و`Download/Install` معطّلان.

**التالي:** تنفيذ `PS4 Storage Port` مع مراجعة APIs ومسارات التطبيق وقيود `openat/O_EXCL/fsync` قبل أي Runtime تفعيل، ثم HTTPS/TLS Transport موثوق مع فحص الشهادات؛ تنزيل Test Fixture صغير فقط على محاكي عند جدواه، وPS4 Hardware Acceptance في النهاية.

---

## تحديث 10 نوفمبر 2026 — StorageAdapter Native بلا صلاحيات كتابة على PS4

**Verdict: MODIFY — نجح كود إدارة ملفات النقل على Host، ولم يُفعّل على PS4.**

- **Code**: `ps4/src/download_storage.h/.c` يعرّف `DsSession` و`DsPort`، وهي طبقة C99 محمولة للتمييز بين `.part` والملف النهائي. تُبنى مع OpenOrbis لاختبار التوافق فقط؛ لا يوجد `PS4 filesystem backend` في التطبيق بعد.
- تقبل فقط `Content ID` و`Version` صالحين لتوليد اسم ملف آمن دون Slash أو Path Traversal. سقف كتلة الكتابة الواحدة **64 KiB**. كتابة حجم زائد أو نشر ملف غير مكتمل مرفوضة.
- تعتمد على **Backend موثوق منفصل** لينفّذ `create_exclusive` و`write_all` و`flush` و`close` والتحقق من SHA-256 الفعلي للملف المخزن و`publish_exclusive` و`unlink_part`. كود Native لا يحتوي حتى الآن Backend PS4 حقيقيًا ولا خوارزمية SHA-256 محمولة.
- لا يعتبر وصول آخر بايت نجاحًا؛ المسار الإجباري: **Full size → fsync/close → verify stored file → publish without overwrite → cleanup owned partial**. فشل أي خطوة يُبقي الحالة غير معتمدة ويحفظ الملف النهائي السابق دون تغييره.
- لا يحذف `.part` أقدم منه عند فشل `create_exclusive`. الفشل في تنظيف ملفه الجزئي بعد نشر الملف النهائي يترك حالة `PUBLISHED + cleanup_pending` قابلة لتنظيف الجزئي فقط، دون حذف النهائي.
- **Host backend في `tests/ui_mocks/storage_smoke.c` تجريبي POSIX**: يكتب بايتات حقيقية في مجلد مؤقت، ويرفض الاستبدال، ويختبر حالات فشل الكتابة و`fsync` و`publish` و`cleanup`، مع اختبار عدّ بايتات `Download Queue`. **مهم:** دالة `verify_sha256` في هذا الاختبار تعتمد مقارنة محتوى Fixture المعروف وToken تجريبي، **وليست تطبيقًا حقيقيًا لـSHA-256**. اختبار SHA-256 الحقيقي السابق منفصل في Python Host harness ولا يثبت Native PS4.
- **Host CI على Code SHA `943d33ebb4095fa451c0346f9c9d362260bf9257`: 213/213 PASS** — https://github.com/obsp081-coder/ps4-updates/actions/runs/42792501024
- **OpenOrbis CI على SHA نفسه: PASS (Compile + PKG header verification)** — https://github.com/obsp081-coder/ps4-updates/actions/runs/42792500963
- **لا Release رسمي جديد، ولا تم تفعيل Download/Install**. حزمة Preview تبقى Read Only. دعم `open/O_EXCL/fsync/link` على FW14، مسار تخزين التطبيق والصلاحيات، وBackend SHA-256 الحقيقي **Unverified**.

**التالي:** Native File Backend آمن خاص بمجلد التطبيق بعد فحص دعم APIs والحدود، ومحقق SHA-256 فعلي، ثم HTTP/TLS Adapter موثوق وربط Queue ببايتات النقل الحقيقية. تعاد اختبارات Host وshadPS4 عند الحاجة؛ **اختبارات PS4 الحقيقي آخر مرحلة بناءً على طلب المستخدم**، وتظل أزرار التنزيل/التثبيت غير مفعلة قبلها.

---

## تحديث إضافي — Native HTTP Response Safety Policy

**تغيير Code:** `3970da3e9183fe4698d652ba806e7f957d7718a0` — أضيفت الوحدة `ps4/src/download_http_policy.h/.c` إلى Native PS4 Build و`tests/ui_mocks/http_policy_smoke.c` و`tests/test_download_http_policy.py`.

- يفحص **Metadata من Parser موثوق لاحقًا** ولا ينشئ HTTP/TLS Session. لا يقبل `tls_authenticated` المفقود، والتطبيق ليس لديه TLS adapter حقيقي حتى الآن.
- يمنع Response غير `200` للتنزيل من الصفر، ويشترط `206` مع Range/If-Range وStrong ETag مطابق للاستكمال.
- يتحقق من `Content-Range` الدقيق من Offset إلى نهاية الحجم الموثوق ومن `Content-Length` المتبقي. يرفض `Transfer-Encoding`، Content Encoding، التوجيهات، وETag الضعيف أو المحقون.
- الاختبارات Native C اصطناعية، لا تثبت HTTPS مصدّقًا أو تحميلًا على الجهاز.
- **الحالة:** تنفيذ وبناء Host المحلي للدالة تم، ونتيجة CI لآخر Code SHA يجب التحقق منها قبل اعتماد نجاح OpenOrbis. إنتاج التطبيق يبقى `Read Only`.

**المتبقي:** Parser حقيقي لترويسات HTTP، وتحقق سلسلة شهادات TLS، ومستودع مصدر مخوّل، وNative StorageAdapter وHash/Partial-state recovery؛ لا يوجد Resume حقيقي أو تنزيل PKG على PS4.

---

## تحديث 10 نوفمبر 2026 — نقل HTTP محلي فعلي للتحقق على Host

**الحالة:** تم إضافة `tests/host_fixture_transfer.py` و`tests/test_host_fixture_transfer.py` كـ**اختبار تكامل على الكمبيوتر فقط**. هذا ليس كود Downloader يعمل على PS4، ولم يُفعّل تنزيل تحديثات ألعاب أو تثبيت PKG.

- ينشئ الاختبار `ThreadingHTTPServer` محليًا على `127.0.0.1` ويوفر محتوى اصطناعيًا من صنع الاختبار.
- العميل يتصل فقط بـ`http://127.0.0.1`، ويرفض المضيف الخارجي وإعادة التوجيه، ولا يستخدم Proxy.
- يرفض `Content-Length` المفقود أو المختلف، `Transfer-Encoding` غير المدعوم، الضغط، رد 404، الاستجابة المقطوعة، وملفات مختلفة البصمة.
- يفحص الحد الأقصى للحجم (8 MiB) والمساحة، ويسجل البايتات المنقولة فعليًا في SHA-256؛ ولا يعتمد حجم الملف أو البصمة على ترويسات المصدر وحدها.
- ينشئ ملف `.part` بشكل حصري، ويفحص SHA-256 قبل نشر ملف نهائي بدون الكتابة فوق ملف موجود. في الفشل يلغي **فقط** الملف الجزئي الذي أنشأه هو.
- كشف الاختبار خطأً كان سيمحو `.part` سابقًا في حالة فشل فتحه؛ تم إصلاحه باشتراط ملكية الملف (`owns_partial`).
- **نتيجة محلية:** `9/9 Host integration tests PASS`. لا تدّعِ اختبارها على محاكي أو جهاز PS4.
- **CI GitHub:** راجع https://github.com/obsp081-coder/ps4-updates/actions?query=branch%3Afix%2Fscanner-cid-and-inventory-capacity؛ يجب التحقق من نتيجة آخر SHA قبل وصف CI بالناجح.

**القيود:** هذا النموذج *ليس HTTPS* لأنه مقصور على localhost للاختبار. لا يوجد Native PS4 `SceHttp` downloader أو `SHA-256` مربوط بـQueue؛ ولا يدعم هذا النموذج Resume بعد إعادة التشغيل. مرحلة Real Transport + `StorageAdapter` Native ما زالت مفتوحة، وFW14 Hardware مؤجل للنهاية.

---

# المرحلة الخامسة — Downloader | حالة التطوير

**تاريخ المراجعة:** 10 نوفمبر 2026
**القرار:** MODIFY — تنفيذ Offline-first مع تأجيل اختبار FW14/GoldHEN الحقيقي للنهاية.
**Code SHA لهذا الإنجاز:** `0d2063752703c87ee842c9ff745057a85a8adca7`.
**تنبيه:** وجود كود Queue لا يساوي تنزيلًا حقيقيًا. لا تنزيل أو تثبيت في Production.

## ما أنجز برمجيًا
- `ps4/src/download_queue.h` و`download_queue.c` يعرّفان `DlqQueue` بـ16 مهمة كحد أقصى، مع هوية `CUSA/Content ID/Version` صحيحة وبلا تكرار.
- State Machine: `QUEUED → ACTIVE → PAUSED/VERIFYING → COMPLETED/FAILED/CANCELLED`.
- تنزيل واحد في الوقت ذاته؛ `VERIFYING` يمنع بدء مهمة أخرى حتى اكتمالها أو فشلها.
- `dlq_space_check` يحسب `download bytes + install workspace + safety reserve`، ويرفض Overflow، ويعيد مقدار العجز. **قيمة Workspace يجب تأكيدها من مصدر موثوق؛ الصفر ليس إثباتًا لكفاية مساحة التثبيت.**
- يمنع تسجيل Bytes تتجاوز حجم الملف المتوقع؛ لا ينقل بيانات ولا يصطنع Progress.
- `Resume` يظل مرفوضًا ما لم تُقدَّم إثباتات حقيقية لثبات المصدر وسلامة الملف الجزئي. التحقق الفعلي من ETag/Range/Hash **غير منفذ**.
- بعد اكتمال عدد البايتات يدخل الملف `VERIFYING`. مجرد اكتمال البايتات **ليس** اكتمالًا موثوقًا. SHA-256 الفعلي غير موصول بعد؛ نتيجة الفحص حاليا مُدخلة من طبقة مستقبلية غير موجودة.
- عند فشل Integrity تبقى المهمة `FAILED`، و`Retry` يبدأ من الصفر مع عداد محاولات؛ لا إعادة استخدام جزئي غير موثوق.
- تمت إضافة `download_queue.o` إلى بناء PS4 لاختبار Compile/ABI فقط؛ لا توجد أي وظيفة Production تستخدم الوحدة لتشغيل نقل، ولا بيانات مهام وهمية في الواجهة.

## الاختبارات
- `tests/ui_mocks/download_queue_smoke.c` و`tests/test_download_queue.py`: فحص نقص المساحة، Overflow، التكرار والسعة القصوى، one-active-job، حدود حجم البايتات، Pause/Resume، رفض Resume غير الموثق، تحقق Integrity الفاشل والناجح **الاصطناعي**، Retry وCancel.
- **Host CI على SHA أعلاه: 202/202 PASS** في https://github.com/obsp081-coder/ps4-updates/actions/runs/42787040073
- **OpenOrbis Build:** راجع https://github.com/obsp081-coder/ps4-updates/actions/runs/42787029684 — لا تعتمد نجاحه قبل وصوله للحالة النهائية.
- **shadPS4:** غير مطلوب لهذه الخطوة لأنها Pure C غير مرتبطة بالواجهة أو APIs؛ يُستخدم لاحقًا لاختبار UI وNetwork lifecycle إن دعمها المحاكي. لا يعوّض GoldHEN FW14.
- **PS4 Hardware:** مؤجل بطلب المستخدم إلى نهاية المراحل، وليس مثبت النجاح.

## القادم (الترتيب الإجباري)
1. `TransportAdapter` بنقل HTTPS حقيقي **لمحتوى اختبار صغير مصرح به فقط**. البحث الأولي وجد مثال OpenOrbis `samples/net_http` يستخدم `SceNet/SceSsl/SceHttp`، لكن الأداء والتحقق من TLS على FW14 غير مؤكدين.
2. `StorageAdapter` بملفات .part وعملية حفظ ذرية وحدود RAM ومساحة حقيقية. لا تلمس ألعاب المستخدم أو ملفات الحفظ.
3. تحقق SHA-256 باستخدام تنفيذ موثوق، `Content-Length`، و`Range/ETag` للـResume مع رفض التغيرات.
4. ربط `Download Queue` بالواجهة حصريًا من مهمة نقل حقيقية، واختبار Cancel/Retry/Resume/Offline/Low storage بملف مصرح به على Host، ثم shadPS4 إن كان مفيدًا.
5. لا تفعّل تنزيل ملفات تحديثات الألعاب إلا بعد مصدر مخوّل وفحص التوافق والحقوق؛ ولا تفعّل Installer قبل Hardware Proof.

**بوابات PS4 المؤجلة:** نقل فعلي بإذن المصدر، صلاحيات التخزين، حجب تحديث النظام، حجم الملفات، تعامل الإلغاء والانقطاع، ثم اختبار التثبيت في المرحلة السادسة.

