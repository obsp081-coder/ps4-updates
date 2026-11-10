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

