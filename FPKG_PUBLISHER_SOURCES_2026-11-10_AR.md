# مصادر تحديثات FPKG — مراجعة الأدلة وخطوة Publisher Registry
**التاريخ:** 2026-11-10
**القرار:** MODIFY: ACCEPT لسجل مراجع الناشرين للعرض اليدوي فقط؛ DEFER لأي Provider يدّعي توفير كل تحديثات FPKG التجارية.
**الحالة الفعلية:** Code Written + Host CI مطلوب. لم يُفَعّل Download/Install أو Live Discovery على PS4.

## نتيجة البحث

### 1. LightningMods/PS4-Store
- المصدر الأصلي: https://github.com/LightningMods/PS4-Store
- الرخصة المعلنة: GPLv3. يوضح README أن Store يعرض تحديثات Homebrew وApps، ويقرر تحديثها حسب MD5 أو Version/SFO وفق الحجم والفئة، ويوفر Pause/Resume/Cancel.
- **الحد:** هذا ليس دليلاً على توفر FPKG patch مطابق لأي نسخة لعبة تجارية معدّلة. كما أن استعمال كود GPLv3 يتطلب مراجعة التزامات الترخيص، ولا يمنحنا حق استخدام CDN أو توزيع ملفات PKG.

### 2. Unrealisedd/FPKGi-OpenOrbis
- المصدر: https://github.com/Unrealisedd/FPKGi-OpenOrbis
- الحالة المعلنة في README v1.03: مشروع GPLv2 Native يستخدم كتالوج HB-Store لـHomebrew وينص على أن **مُطوّره** لديه إذن مكتوب من مسؤولي PKG-Zone/HB-Store. إذن شخص آخر **ليس موافقةً لبرنامجنا**.
- يصف المشروع اختبارًا حقيقيًا على FW14 لنسخ سابقة، ويؤكد أن v1.03 لم تجتز Hardware Qualification النهائي (حسب README في وقت البحث). هذا دليل Developer Claim ومراجعة Source وليس اختبارنا على جهاز PS4.
- الكتالوج الأساسي خاص بتطبيقات Homebrew؛ يستبعد CUSA/Retail-style IDs في سياسته المعتادة. لا يمكن تحويله إلى حل تحديثات FPKG تجارية.
- تراخيص ومصادر المشروع لا تُنسخ هنا؛ استفدنا من تصور Policy + offline checks فقط.

### 3. الكتالوجات غير الموثقة
- وجود CUSA على موقع أو GitHub لا يثبت نوع PKG ولا ملكيته ولا توافق Base Game/Patch. لا نبحث عن أو نوزع FPKG غير مصرح بها، ولا نستخدم Sony CDN بعد قرار FPKG-only.
- سير عمل CI السابق كان يسحب Metadata من كتالوجات أطراف خارجية بحثًا عن CUSA تجارية؛ أُوقف لأنه لا يستند إلى مصدر ترخيص مثبت ولا يؤدي لاكتشاف تحديثات آمنة. البديل الآن Offline فقط.

## ما نُفّذ

1. `ps4-auto-updater-v0.3/fpkg_publisher_sources.py`: `review_publisher_origins(report, policy)` يراجع Source Page من تقرير `catalog_ingest.ingest` ضد سجل ناشرين مستقل. يقبل فقط صفحات GitHub Release ذات Repo مضبوط، ويطلب Evidence URL من **نفس Repo** وتذكرة مراجعة بشرية. لا يجري أي اتصال بالإنترنت، ولا يقرأ ملفات PKG، ولا يعيد رابط تنزيل.
2. `ps4-auto-updater-v0.3/source_registry/reviewed_publishers.json` يبدأ بقائمة **فارغة**. لم نُدخل اسمًا أو حقًا كأنه موثوق قبل الدليل.
3. `ps4-auto-updater-v0.3/update_source_registry.py`: خيار CLI `--source-policy` لإضافة صفة `publisher_origin_review` إلى الترشيحات. النتيجة `PUBLISHER_REFERENCE_REVIEWED_ONLY` أو `UNREGISTERED_SOURCE`؛ في الحالتين تظل `FPKG_SUBMISSION_UNVERIFIED` وتبقى `download_allowed=false` و`install_allowed=false`.
4. `tests/test_fpkg_publisher_sources.py`: اختبارات عدم التزوير وRepo المشابه وURL به Query وملفات Evidence غير سليمة وDuplicate Records وحقول Actions غير موثوقة.
5. `.github/workflows/fpkg-source-audit.yml`: **إلغاء مراجعة كتالوجات CUSA التجارية الخارجية**، واستبدالها باختبارات سياسة Offline لا تستخدم شبكة ولا تنزّل PKG.

## شروط إضافة ناشر حقيقي إلى Registry

- إثبات أن Repo تابع لمالك تطبيق Homebrew، وأن لديه حق إصدار ملفات التحديث المعنية، مع صفحة ترخيص منشورة أو موافقة مباشرة موثقة. وجود LICENSE لكود المشروع لا يثبت وحده ملكية محتوى اللعبة أو إذن إعادة توزيع FPKG.
- تحديد CUSA + Content ID + نسخة Base/Update بدليل مستقل من ملفات يملكها الناشر، لا مجرد JSON submitted.
- تحديد إن كان الإصدار `patch` بالمعنى الذي يقبله PS4 وليس Base PKG جديدة. تحديث نسخة Homebrew كاملة لا يساوي Patch دائمًا.
- تحصيل تصريح للوصول الآلي لMetadata، وبشكل مستقل لأي Download URLs أو إعادة توزيع. من الأفضل رابط أصلي من الناشر بدل Mirror.
- بعد مراجعة البشر، يبقى Registry مرجع أصل فقط؛ لا منح Download/Install من CLI. النقل على FW14 يتطلب إثبات TLS وStorage وInstaller وتوافق حقيقي.

## المرحلة التالية

1. تحقق من مصدر Homebrew ناشر واحد مع صلاحية استخدام Metadata مكتوبة/واضحة؛ ابدأ ببرنامج Homebrew مفتوح المصدر، لا بألعاب FPKG التجارية.
2. اطلب أو استخرج **Manifest Patch** قابل للتحقق من الناشر: Title ID، Content ID، App Version، Required Base Version، Min FW، Size وSHA-256 وتأكيد الحقوق.
3. ابْنِ Read-only Provider مقابل مصدر حقيقي **بعد الإذن** مع TLS/Redirect/Size limits، دون تفعيل Download. عرِض `No trusted update` عند غياب السجل.
4. إثبات Type/Compatibility وNetwork في PS4 14 Hardware ثم Stage 5 Storage + Installer الحذر.

**لا دليل حتى الآن على كتالوج شامل قانوني لتحديثات الألعاب التجارية المعدّلة.** عدم ملء Registry بمصادر مزعومة خيار أمان متعمد.
