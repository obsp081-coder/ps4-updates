# المرحلة الثالثة — مراقبة GitHub Releases للناشرين المراجَعين (Metadata Only)
**تاريخ التدقيق:** 10 نوفمبر 2026
**Verdict:** MODIFY — ACCEPT لمراقبة Metadata محدودة، DEFER لأي Download أو FPKG Patch claim.
**النطاق المستهدف:** Game Update Manager FPKG-only على PS4 FW14 + GoldHEN.
**دليل الاختبار:** مراجعة Source Code + Host Tests/CI؛ لا اختبار Native Network Runtime على PS4.

## لماذا لا نضيف أي رابط تنزيل مباشرة؟
وجود اسم `.pkg` في GitHub Release لا يثبت ما إذا كان PKG أساسياً (Base App/Game)، أو Patch FPKG، أو Retail/DLC. رقم Version وCUSA وحدهما لا يثبتان توافق FPKG مع نسخة Base مثبتة. الأنواع تستلزم قراءة Package Header/param.sfo ثم أدلة Binding/حقوق/سلامة مستقلة. لا يوجد الآن كتالوج شامل لملفات تحديث FPKG تجارية مصرح بها جرى إثباته للمشروع.

**مراجعة مصادر أصلية بتاريخ 10 نوفمبر:**
- LightningMods/Itemzflow: https://github.com/LightningMods/Itemzflow/releases — الإصدارات تتضمن tag 1.08 بتاريخ 2026-05-31، ولم تظهر ملفات Assets في عينة أول 5 Releases من GitHub API، فلا يصح اعتبارها مصدر Patch PKGs.
- Unrealisedd/FPKGi-OpenOrbis: https://github.com/Unrealisedd/FPKGi-OpenOrbis/releases — أظهرت Releases v1.00–v1.03 ملفات `.pkg` و`.sha256`، لكن README يصفها كإصدارات تطبيق FPKGi Native، ولا يثبت أنها PS4 Patch Packages متوافقة مع أي لعبة FPKG. البيان الصادر عن المشروع يقول إن الإذن المكتوب لاستخدام HB-Store يخص مُطوِّره، وليس تصريحاً لنا.

## الملفات الجديدة

- `ps4-auto-updater-v0.3/fpkg_github_release_observer.py`: أداة منفصلة صريحة `--live` لا تتصل بأي مصدر غير موجود في `source_registry/reviewed_publishers.json`، وترفض Redirects وتستخدم HTTPS مع Certificate Verification من Python SSL، وتفرض مهلة 12 ثانية وحد استجابة 256KiB، و20 Release و64 Asset كحدود قصوى لكل Release. ترفض JSON مكرر المفاتيح أو المحتوى غير الصحيح أو Tags غير آمنة، ولا تمرر تنزيلات أو Body النصي غير الموثوق إلى Output.
- `ps4-auto-updater-v0.3/tests/test_fpkg_github_release_observer.py`: اختبارات Host محلية ببيانات غير حقيقية، لا تقوم بأي طلب شبكة.
- `.github/workflows/fpkg-source-audit.yml`: إضافة اختبارات الأداة إلى Workflow تدقيق المصادر Offline. تبقى `source_registry/reviewed_publishers.json` فارغة لحين ثبوت صلاحية ناشر واحد.
- النتائج `mode=GITHUB_PUBLISHER_RELEASE_METADATA_ONLY`, `download_allowed=false`, `install_allowed=false`, `rights_verified=false`, `compatibility_confirmed=false`. حتى لو احتوى Release على ملف PKG، `package_kind_verified=false` و`patch_compatible=false` دائماً. لا يُنتج Link لتحميل package ولا يُعرض إصدار على أنه تحديث مثبت.

## طريقة التشغيل المقصودة لاحقاً

1. يراجع مسؤول المشروع حقوق ناشر Homebrew ومطابقة Repo والـCUSA ويضيفه لسجل Evidence وفق مخطط `fpkg_publisher_sources.py`. لا يُقبل مجرد Repo مشهور أو وجود LICENSE لكود منفصل.
2. عند السماح، يمكن فحص GitHub public Metadata يدوياً:
   `python3 ps4-auto-updater-v0.3/fpkg_github_release_observer.py --publisher-registry ps4-auto-updater-v0.3/source_registry/reviewed_publishers.json --live --out observed_releases.json`
3. إذا كان السجل فارغاً فلا يحصل أي Network Request حتى مع `--live`.
4. التقرير يبيّن فقط tags المرصودة وعدد الملفات التي تنتهي بـ`.pkg`. لا تُستخدم Tag كـ`APP_VER` ولا عدد ملفات PKG كتحديث متاح. يجب إثبات Package Category (مثلاً `gp` أو `gdp` في param.sfo)، نوع FPKG، Version Binding، Content ID، وحقوق وصلاحية التنزيل بصورة مستقلة.
5. أي فشل API أو Timeout = `SOURCE_UNAVAILABLE`، وليس `NO_UPDATE` ولا موافقة صامتة.

## البوابات المتبقية

- **Authorized source:** لا يوجد ناشر أو لعبة تجارية مسجلة ومرخصة كـFPKG Patch Provider حتى الآن.
- **Artifact identity:** لا توجد حزمة Patch مخوَّلة اختبرت على PS4 14.00. المفتاح `package_kind` في Manifest غير كافٍ، ويجب فحص محتوى PKG من مصدر مرخص وإثبات Binding.
- **Native transport:** Native `__ORBIS__` Downloader مغلق وفحص شهادات SceHttp/SceSsl غير مثبت على FW14.
- **Storage/Install:** Native Backend وInstaller لم يجتازا Hardware Proof. CI ليس اختبار PS4.
- **UI/Release:** ممنوع تفعيل Download/Install أو إصدار PKG على أنها نهائية؛ PR #1 تبقى Draft.

**الخطوة التالية:** إثبات ناشر واحد وحزمة **Patch FPKG** فعلية مصرح بها، مع Package Header/SFO + Hash + Binding، ثم ربط Metadata للعرض read-only أولاً. لا نستخدم Retail أو روابط FPKG مجهولة المصدر.
