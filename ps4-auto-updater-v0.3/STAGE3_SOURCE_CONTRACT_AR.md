# المرحلة الثالثة — Native Source Contract (فصل المصدر عن التوافق)
**الحالة:** مُنفَّذ عقد تسليم بيانات المرشحين داخل Native PS4، **غير مربوط بمصدر إنترنت فعلي**. المرحلة الرابعة PAUSED.

## ما الذي يُقبل من المصادر؟
- `UiSourceCandidate` داخل `ps4/src/ui_source_bridge.h` لا يحمل سوى `CUSA` و`Content ID` و`Version` ونوع مصدر التصريح:
  - `UI_SOURCE_RETAIL_HTTP`: معلومات تاريخية قد تظهر في Sony Retail XML عبر HTTP بلا مصادقة للنقل. **Metadata Only**.
  - `UI_SOURCE_FPKG_SUBMITTED`: ترشيحات مرسلة من أطراف خارجية؛ لا يوجد تحقق من صحة FPKG أو إذن التوزيع. **Unverified**.
- `ui_source_publish_candidates` يراجع بنية ID/Version ويفحص التكرار `CUSA+Version+SourceKind` دفعةً واحدة. إذا فشلت أي خانة، لا تتبدل الصفوف السابقة. سعة العرض القصوى `24` مرشحًا.
- لا يمكن عرض حجم أو حد Firmware مجهول على أنه معلوم: المدخل الحالي لا يقبلهما، و`size_bytes=0` و`minimum_fw` فارغ حتى تتوفر شهادة مصدر مستقلة. لا يتم تصنيع أحجام أو تواريخ أو متطلبات Firmware.
- لا يصبح أي سجل `UI_UPDATE_VERIFIED`: كل مرشح `UI_UPDATE_UNKNOWN`. حتى `UiUpdateInfo` الخام يرفض `UI_UPDATE_VERIFIED` في المرحلة الحالية. التنزيل والتثبيت **لا يوجد لهما أي استدعاء في هذا المدخل**.
- شاشة Details تُظهر `RETAIL` أو `FPKG?` بجانب رقم التحديث *إن وُجد مرشح حقيقي*، مع نص التوافق غير المؤكد. لقطات Host التجريبية فقط تستخدم سجلًا اصطناعيًا؛ التطبيق الإنتاجي لا يُنشئ مرشحات من تلقاء نفسه.

## معايير قبول مصدر إنترنت مستقبلي قبل أي استدعاء من PS4
1. **حقوق المصدر:** شرط استخدام واضح يسمح بالوصول الآلي إلى *metadata* المطلوبة؛ ترخيص MIT لكود أداة بحث لا يمنح تلقائيًا حقوق تنزيل/إعادة توزيع ملفات Sony أو FPKG.
2. **النقل:** `HTTPS` مع تحقق شهادة واسم المضيف، رفض Redirect إلى شبكة داخلية، Timeout وحد أقصى للاستجابة. **Sony HTTP القديم غير مؤهل كمصدر موثوق للتحميل**.
3. **الهوية:** Provenance و`CUSA` و`Content ID` وVersion، وعزل Retail عن FPKG قبل أي فحص توافق.
4. **الحدود:** أقصى 24 مرشحًا للواجهة، حد على الحجم الكلي وبنية XML/JSON، حد لمعدل الطلبات، Cache TTL صريح، ومعالجة `404` منفصلة عن `TIMEOUT/LOOKUP_FAILED`.
5. **أمان النظام:** لا تنزيل `PKG` أو Firmware system update، ولا Install، ولا إرسال بيانات الألعاب الخاصة بالمستخدم بلا موافقة. لا تحويل `HTTP 200` إلى `compatible`.

## الدليل
- [مرحلة Source Registry](https://github.com/obsp081-coder/ps4-updates/commit/3564073cc44abf218716357d6e9a7fc87395c36b) تستخدم Python للتطوير وOffline فقط.
- [Native source bridge](https://github.com/obsp081-coder/ps4-updates/commit/c814d74190b0e383deadeced60c5104230686189) فصل الهوية ونوع المصدر، وأضاف اختبار C مستقل.
- [Native Details metadata tag](https://github.com/obsp081-coder/ps4-updates/commit/c6c3c9a31c8a88917c924bca4f6e0bfbab262e48) باستخدام Mock فقط، ثم [إصلاح تهيئة Host C++](https://github.com/obsp081-coder/ps4-updates/commit/cb6240852dc3d6b9e982836f3ba2d239bc5e2dd0).
- [CI Host](https://github.com/obsp081-coder/ps4-updates/actions/runs/42699845536) و[Native Build](https://github.com/obsp081-coder/ps4-updates/actions/runs/42699837258). **تُراجع الحالة النهائية قبل الادعاء بالنجاح**.
- مصدر تاريخي للمسار `HTTP`: https://www.psdevwiki.com/ps4/Online_Connections
- مرجع OSS لوظيفة Metadata والقيود والترخيص MIT: https://github.com/andshrew/PS4-Updates-Python

## الخطوة القادمة، والبوابة غير المحققة
لم نعثر بعد على **Provider مصرح وآمن** يعيد تحديثات Retail/FPKG بالقدر المطلوب على جهاز FW14. بعد إثبات صلاحية المزود والأمان، نبني Network Layer موثوقًا وموصولًا بـ`UiSourceCandidate`، ونختبره على Host/Emulator ثم PS4 الحقيقي. **لا** ننقل الكود التجريبي Python إلى PS4 كما لو كان يعمل هناك.

**القرار: MODIFY — مدخل العرض Native متحقق بالاختبارات، لكن اكتشاف Live موثوق لم يتم.**
