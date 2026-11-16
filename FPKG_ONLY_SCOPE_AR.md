# PS4 Auto Updater — FPKG Only Scope / Decision Record
**الموافقة:** 10 نوفمبر 2026
**القرار:** ACCEPT — حذف Retail Update Discovery/Download/Install من مسار المنتج.
**ملاحظة:** دعم FPKG-only لا يعني وجود تحديثات FPKG قابلة للتنزيل الآن.

## السلوك المعتمد

1. يقرأ Scanner الألعاب والـCUSA وparam.sfo المتاحين. **هذه البيانات وحدها لا تثبت أن اللعبة FPKG.** تُعرض الألعاب غير المصنفة في مكتبة البيانات التشخيصية فقط إلى أن يثبت مصدر تحديد النوع. لا تُنشأ مهمة تنزيل أو تثبيت نتيجة لهذا الفحص.
2. لا يستدعي المسار المعتمد Sony Retail Patch Lookup ولا Sony CDN. لا تُستخدم Retail Catalog أو Retail PKG للتعويض عن نقص تحديثات FPKG.
3. يقبل Source Index فقط تقارير `catalog_ingest.ingest` المصنفة `fpkg_patch` **كتصريحات غير موثقة**، ويقدم `UNVERIFIED_FPKG_CANDIDATES` بدل ادعاء توفر تحديث صالح.
4. يقبل Native UI Source Bridge فقط `UI_SOURCE_FPKG_SUBMITTED`. ويرفض Retail Candidate حتى لو شارك نفس CUSA/Content ID. Model يرفض `UI_SOURCE_RETAIL_HTTP` ويحفظ قائمة التحديثات السابقة عند أي خطأ (transactional).
5. Compatibility Engine يرفض Retail base أو Retail patch صراحةً (`RETAIL_UNSUPPORTED`) حتى عندما يتطابق نوع الاثنتين. القاعدة مجهولة النوع = `KIND_UNCONFIRMED` ولا يمكن تنزيلها.
6. Review CLI يستخدم Inventory مباشرة دون Retail Comparison. إن كانت `package_kind` مفقودة فـ`BASE_PACKAGE_KIND_UNCONFIRMED`، وإن كانت `retail` فـ`RETAIL_UNSUPPORTED`. `fpkg` الواردة من التقرير ليست برهانًا مستقلًا؛ حالات Manual Review لا تفتح Actions.
7. UI تبقي كلمة `FPKG?` للدلالة على *تصريح غير موثوق*، وتحافظ على العبارة «لا يوجد تحديث موثوق متاح» حين لا تتوفر مصادر. التنزيل/التثبيت لا يزالان مغلقين تمامًا على FW14.

## التغييرات الفعلية

- `ps4-auto-updater-v0.3/update_source_registry.py`: Output Schema 2، `package_policy=fpkg_only`، حذف دعم `--retail-report` ودمج Sony Metadata.
- `ps4-auto-updater-v0.3/fpkg_review.py`: Input Inventory بدلاً من Retail Comparison، وفحص Kind الصريح وتعطيل الأعمال الجانبية.
- `ps4-auto-updater-v0.3/ps4/src/ui_source_bridge.c`: رفض Retail Candidates.
- `ps4-auto-updater-v0.3/ps4/src/ui_models.c`: رفض Retail في النموذج. Legacy enum value يبقى حفاظًا على ABI لكنه غير مقبول في Output. UNSPECIFIED عرض read-only فقط، لا مصدر تحديث موثوق.
- `ps4-auto-updater-v0.3/ps4/src/ui_compatibility.c/.h`: Reason `UI_COMPAT_RETAIL_UNSUPPORTED` مستقل عن mismatch.
- `ps4-auto-updater-v0.3/ps4/src/scanner_ui.cpp` و`ps4/tools/generate_ui_arabic.py`: إزالة Label RETAIL من تفاصيل التحديث وإضافة توضيح أن التحديثات المستهدفة FPKG فقط. لا تغيير للتصميم الملكي.
- تم تحديث Host Fixtures واختبارات Registry/FPKG Review/Source Bridge/Compatibility/Model/Arabic UI.

## الحدود والأدلة المطلوبة قبل اعتماد أي Download/Install

- **Blocker 1:** لا توجد حاليًا قاعدة موثوقة شاملة لمصادر FPKG مصرح بها. Source URL وصفحة metadata ليست دليل ملكية أو ترخيص أو توفر PKG.
- **Blocker 2:** إثبات أن Base game FPKG وليس مجرد CUSA متطابق. يحتاج أسلوباً عملياً على FW14 مع سجلات أو خصائص الحزمة ومدى موثوقيتها.
- **Blocker 3:** Independent update identity/patch binding، base version، region، min FW، integrity checks؛ لا تقبل `declared_package_kind` كدليل توافق.
- **Blocker 4:** TLS verification الحقيقي في SceSsl وCrash-safe Storage Backend وInstaller على FW14 غير مثبتين بعد، وكلهم مغلقون في Native Build.
- **Blocker 5:** اختبار PS4 الحقيقي على Hardware، مع عدم المساس بأي Installed Game أو Save Data.

## المستبعد والموثّق فقط للتاريخ

- `official_patch_lookup.py` وأي اختبارات/ملفات من البحث القديم عن Retail تبقى مواد تراثية/للمقارنة التاريخية خارج مسار FPKG-only الأساسي؛ لا يجوز استعمالها كموفر تحديثات للمنتج. لا تتغير حماياتها ولكنها ليست مصدرًا معتمدًا.
- `update_matcher.py` مع demo catalog هو تمرين Offline قديم للبيانات التخيلية، **ليس مسار البحث والتثبيت** ولا يعطي صلاحيات تنزيل. سيتم أرشفته نهائيًا بعد أن يثبت Scanner/FPKG Provenance على Hardware.

**إثبات الحالة:** Host Tests وOpenOrbis Build لا يثبتان Runtime أو صلاحية تحديث لعبة فعلية. يجب الفصل بين Code Written وHost Tested وOpenOrbis Compiled وReal PS4 Tested في أي إعلان إنجاز.
