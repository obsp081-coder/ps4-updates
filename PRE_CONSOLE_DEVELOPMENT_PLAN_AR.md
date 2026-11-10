# خطة استكمال PS4 Auto Updater دون توفر الجهاز — 10 نوفمبر 2026

**Verdict: MODIFY — نستكمل العمل القابل للتحقق على Host/CI الآن، ونمنع ادعاء اكتمال ميزات FW14.**
**الفرع:** `fix/scanner-cid-and-inventory-capacity` ضمن Draft PR #1.
**الجهاز المستهدف:** PS4 FW14.00 + GoldHEN.
**نطاق المنتج:** Native PS4، FPKG-only، لا Sony CDN ولا Retail Update Provider ولا تنزيل غير مصرح به.

## التمييز الضروري بين الأدلة

- **Code Written:** ملفات أُضيفت للمستودع، وقد تحتوي أخطاء قبل CI.
- **Host Tests PASS:** بيانات ومقابس/خوادم وأقراص افتراضية على Linux، لا صلاحيات نظام PS4.
- **OpenOrbis Build PASS:** نجاح تجميع EBOOT وحزمة PKG، لا يعني أن التطبيق يفتح على FW14 أو يستطيع تنفيذ APIs.
- **Real PS4 Tested:** يتطلب ملف Log/نتيجة على جهاز حقيقي؛ لا يحق استنتاجه من الأنواع الثلاثة السابقة.

## ما نُفّذ قبل وجود السوني

| المسار | الحالة | الدليل والقيود |
| --- | --- | --- |
| Native Scanner + UI + GoldHEN FTP | Verified سابقًا على الجهاز في حدود v0.6.5؛ CI مستمر | أربع ألعاب Base من تسع سجلات SFO، مع فحص External غير مكتمل؛ **ليس** FPKG Proof |
| Royal Blue UI + Arabic + Icon/Cache | Host CI; بعض السلوك جُرّب على الجهاز سابقًا | اختبار دقة الأداء وموضع صورة الخلفية على TV لم يكتمل |
| Compatibility + FPKG-only + Source Registry | Host tested، no install | Identity/region/version/firmware وكشف Retail/Unknown؛ لا ناشر معتمد حتى اليوم |
| Integrity/Download/Resume/Storage policies | Host tested فقط | Native TLS/Storage backend غير متاحين؛ نجاح Host لا يبرر تفعيلهما |
| PKG Header/SFO/SHA-256 offline | Host tested | Evidence Report لا يثبت الصلاحية أو FPKG/Signature |
| Installed PKG Type Diagnostic | Native build optional | `BREW01998` مستقل عن التطبيق المعتاد؛ يسجل `PKG_SELF_HEADER` أولًا، ثم CUSA إن وجدت؛ **غير مجرب على الجهاز** |
| Native Download/Install | DEFER — hard disabled | عدم وجود Source License/Installer/SceSsl proof |
| Release PKG كاملة | REJECT حتى تحقق البوابات | لا Merge للـDraft ولا إصدار كـUpdater جاهز |

## تصحيح أخطاء حقيقي في دورة العمل الجديدة

1. عدّلنا `fpkg_review.py` لكي **لا ينهار تقرير المكتبة بالكامل** إذا احتوى `CONTENT_ID` أو `APP_VER` ناقصًا في إحدى الألعاب؛ الحالة تُحجز كـ`BASE_CONTENT_ID_UNCONFIRMED` أو `BASE_VERSION_UNCONFIRMED` أو خطأ هوية محدد.
2. حظرنا `gp` و`ac` كـBase Games، والأقسام غير المعروفة تبقى Blocked. لم نسمح لتلميح Header `FAKE_FLAG_CANDIDATE` بمنح ثقة.
3. حدّدنا عدد السجلات في CLI، ورفضنا مفاتيح JSON المكررة، و`schema` منطقيًا، وحقول Action/حقوق مزورة في `catalog_ingest`.
4. أوقفنا Workflow تقارير Retail **في فرع التطوير فقط**؛ لا Schedule ولا جلب Sony ولا `git push` في هذا الفرع. **لا تزال نسخة Workflow القديمة على main** تعمل وفق إعدادات Main، ولم نغيّرها لعدم دمج PR Draft. تعطيلها على main قرار منفصل قبل أي دمج.
5. طورنا `BREW01998` ليقرأ **Header الحزمة الخاصة به أولًا**، ليصبح اختبار الجهاز لاحقًا مستقلًا عن امتلاك أي لعبة تجارية أو وجود CUSA.
6. أضفنا اختبارات CI لحماية هذه الحدود ومنع رجوع Workflow Retail إلى مسار المنتج.

## ما نستطيع تحسينه بدون جهاز — أولويات متبقية

| الأولوية | مهمة Host/CI | دليل الإغلاق |
| --- | --- | --- |
| P0 | تثبيت جميع اختبارات Host + OpenOrbis Package Build لكل Commit | CI Green على آخر HEAD، بلا تحذيرات جديدة؛ ومراجعة Diff |
| P0 | مراجعة أمنية لمسارات Input/Metadata وحقوق Source | اختبارات spoofing/duplicate/oversize/unknown حقول وحفظ Default Deny |
| P1 | اختبار فشل الشبكة/ETag/Range/TLS على Host وربط C callbacks | Fixture محلي موثق؛ لا ادعاء Native Network على PS4 |
| P1 | فحص سلوك Partial File وDisk Full وCancel/Resume/Integrity على Host | اختبارات مؤقتة بعزل الملفات وSHA-256، بلا Sandbox PS4 |
| P1 | اختبارات Regression للواجهة/العربية/الإدخال والكاش حتى 256 لعبة | Host screenshots/smoke وبيانات اصطناعية معلّمة |
| P1 | إيجاد **ناشر Homebrew مصرح** يملك Patch PKG حقيقية، بعقد استعمال Metadata وHash | Evidence Link وتذكرة مراجعة؛ إذا لا يوجد مصدر، لا نخترع محتوى |
| P2 | Packaging وUser Guide وCrash-safe Diagnostics ومراجعة التراخيص | Artifact تجريبي بManifest وSHA، من دون إعلان Release |
| P2 | مراجعة التكاليف والأداء وإزالة Legacy تدريجيًا بعد Regression | لا تحذف API مستخدمة أو مكتبات لازمة بلا دليل |

**ملاحظة:** هذه قائمة تطوير؛ الإنجاز الفعلي لا يُستنتج من وجود القائمة. لا يمكن تحويل مجهود Host وحده إلى دعم شامل لتحديثات FPKG إذا لم يتوفر مصدر مصرح أو PKG Patch صالح.

## ما يجب أن يبقى مؤجلاً حتى يعود الجهاز

1. اختبار بناء `BREW01998` المنفصل على FW14 GoldHEN وتقرير:
   `/data/PS4AutoUpdaterProbe/diagnostic_v065.txt` و`inventory_v065.json`.
   **أول معيار:** `PKG_SELF_HEADER BREW01998` (قد يرجع `OPEN_FAILED`؛ هو نتيجة مهمة).
2. اختبار فتح/إغلاق Native UI، وموثوقية Scanner/GoldHEN FTP وExternal Storage وتبعات Exit/VideoOut.
3. إثبات Installed vs Metadata وتحديد نوع FPKG عمليًا بدليل مستقل، لا SFO/8-byte flags.
4. تفعيل Native HTTPS فقط بعد التأكد من Cert Chain/Hostname/Timeout/Cancel على FW14؛ وتجربة تنزيل Test Fixture صغير مصرح، لا PKG لعبة.
5. Native Storage وRecovery (No-Overwrite/Flush/Partial) على وسيط آمن.
6. تجربة تثبيت واحدة لحزمة Patch مخولة ومربوطة بBase مخوّل؛ مراقبة Install API/Status/Error؛ لا تعديل `app.db` مباشرة.
7. اختبار الفشل، الأداء، إعادة التشغيل، التخزين المنخفض، قطع الاتصال، الاستئناف، مراجعة حفظ الألعاب وبيانات المستخدم قبل Final Release.

## شروط تسليم التطبيق النهائي

- لا مصدر موثوق = `NO_TRUSTED_UPDATE_SOURCE`، ولا Download/Install.
- لا إثبات FPKG أو Binding/Source Rights = Blocked.
- `UNKNOWN` ليس نجاحًا، وHost Tests لا تُظهر Firmware Support.
- لا حذف تلقائي للـPKG إلا بعد نجاح تثبيت يبلّغ عنه نظام PS4، وليس بعد اكتمال نقل البايتات.
- يبقى الفرع Draft إلى أن تغلق Gates وفق أدلة Hardware وخط تزويد Update قانوني.

## الروابط
- Draft PR: https://github.com/obsp081-coder/ps4-updates/pull/1
- بناء اختبار FW14: https://github.com/obsp081-coder/ps4-updates/blob/fix/scanner-cid-and-inventory-capacity/FW14_HEADER_DIAGNOSTIC_INSTALL_AR.md
- FPKG Scope: https://github.com/obsp081-coder/ps4-updates/blob/fix/scanner-cid-and-inventory-capacity/FPKG_ONLY_SCOPE_AR.md
