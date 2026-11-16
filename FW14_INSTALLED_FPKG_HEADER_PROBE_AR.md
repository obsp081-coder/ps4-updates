# فحص نوع اللعبة المثبتة على PS4 FW14 — PKG Header Evidence (قراءة فقط)
**تاريخ البحث:** 2026-11-10
**القرار:** MODIFY — ACCEPT لفحص تشخيصي opt-in من أول 8 بايت، DEFER لإعطاء حكم FPKG مؤكد أو تمكين تنزيل/تثبيت.
**حالة الدليل:** مراجعة كود مشروع خارجي + إضافة Host C Tests + Cross Compile إذا نجح CI لاحقًا. **لم نجرب الفحص الجديد على جهاز PS4 حقيقي.**

## الدليل الأصلي الذي غيّر ترتيب الأولويات
راجعنا كود Itemzflow مباشرة، لا لقطة واجهة أو ادعاء عام:
- [Itemzflow: ls_dir.cpp](https://github.com/LightningMods/Itemzflow/blob/main/itemzflow/source/ls_dir.cpp): `AppExists()` يهيّئ مسارات `/user/app/<TITLE_ID>/app.pkg` أو `/mnt/ext0/user/app/<TITLE_ID>/app.pkg` أو `/mnt/ext1/user/app/<TITLE_ID>/app.pkg`، ثم يرسل المسار إلى `is_fpkg(...)` في الفهرسة.
- [Itemzflow: utils.cpp](https://github.com/LightningMods/Itemzflow/blob/main/itemzflow/source/utils.cpp): `is_fpkg` يفتح الحزمة ويقرأ struct من `magic` و`flags` من بدايتها لتمييز FPKG بشكل إرشادي.
- [Itemzflow: utils.h](https://github.com/LightningMods/Itemzflow/blob/main/itemzflow/include/utils.h): ينشر أنماط flags `0x00000001` و`0x83000001` و`0x81000001`. قررنا التعامل معها **كنمط Header مرصود فقط** وليس إثبات توقيع/مصدر/صلاحية.
- Itemzflow مشروع GPL-3.0. **لم ننسخ مصدره** إلى تطبيقنا؛ أنشأنا parser أصليًا صغيرًا يعتمد على صيغة Header ومقارنة قيم معروفة. لا نستخدم SDK مملوكًا ولا نضيف مكتبات runtime.
- [ps4pkginfo](https://github.com/antoniohauren/ps4pkginfo) يستطيع قراءة Header/SFO من ملف PKG، ولكنه يوضح أن Metadata لا تثبت صحة التوقيع أو سلامة محتوى PKG. وهو أداة PC وليس إثباتًا لصلاحية القراءة على PS4.
- ملاحظة: وصول Itemzflow إلى هذه الملفات في بيئته **لا يثبت** صلاحيات نفس المسارات لتطبيقنا على FW14 مع GoldHEN أو على كل جهاز وتخزين خارجي.

## التغيير الفعلي
1. `ps4-auto-updater-v0.3/ps4/src/pkg_header_evidence.h`: Parser C99 لا يقرأ أي ملفات، يفحص أول 8 بايت وPKG Magic `7F 43 4E 54` وBig-endian flags. الحالة `FAKE_FLAG_CANDIDATE` أو `NONFAKE_FLAG_CANDIDATE` أو `OTHER_FLAGS` أو Error. لا يقبل Flags مجهولة كـFPKG.
2. `ps4-auto-updater-v0.3/ps4/src/ftp_scanner_v05.c`: بعد FTP Read-only Scanner، هناك دالة تشخيص **مقفلة افتراضياً** خلف `__ORBIS__ && PS4_ENABLE_EXPERIMENTAL_PKG_HEADER_PROBE`. لا تنادي أي Install API، ولا تقرأ ملفات اللعبة كاملة، ولا تفحص إلا 8 بايت لكل Base Game. تتطلب `O_NOFOLLOW` و`O_NONBLOCK` وفتحًا للقراءة فقط و`fstat` يثبت أنه ملف عادي. الفشل يعيد Unknown دون افتراض Retail أو FPKG؛ المسارات الخارجية ليست مضمونة.
3. ملف `inventory_v065.json` لا يغيّر schema 6، ويضيف `fpkg_detection_confirmed:false` و`pkg_header_probe_mode` وحقول لكل Game:
   - `package_kind: "unknown"` و`fpkg_confirmed:false` دائماً، حتى لو تمت قراءة Flag.
   - `package_kind_evidence: "SFO_NOT_PROOF"` و`pkg_header_observation` و`pkg_header_flags_observed` و`pkg_header_location`.
   - Metadata records تبقى `package_kind:"unknown"`، ولا يفتح أي زر تنزيل.
4. `tests/test_pkg_header_evidence.py`: Host C parser checks للأنماط المعروفة والجهولة وHeader خاطئ وأطوال ناقصة.
5. `tests/test_ftp_scanner_v05.py`: Fixture GoldHEN FTP يعيد Inventory كاملًا لكن `package_kind=unknown`، ويؤكد أن `fpkg_review` يعيد `BASE_PACKAGE_KIND_UNCONFIRMED` ويمنع Download/Install.
6. `tests/test_fpkg_patch_evidence.py`: حتى لو أضيف إلى Inventory حقل تجريبي `FAKE_FLAG_CANDIDATE`، لا يتحوّل إلى حكم Base FPKG مؤكد.
7. `.github/workflows/ps4-build.yml`: بعد Normal PS4 Build يحاول **ترجمة** المسار opt-in منفصلًا في object فقط؛ **لا يربطه** في eboot العادي ولا ينشر PKG تجريبية للمستخدم.

## لماذا لم أفعّله في الإصدار العادي؟
- لا يوجد Real-hardware test موثق للـ`open/fstat/read` على المسارات الثلاثة تحت Firmware 14.00 وGoldHEN مع بيئة تطبيقنا.
- قد تكون بعض الألعاب على External Storage أو حزم مقسّمة/مثبتة بشكل مختلف، وقد لا يكون `app.pkg` متاحًا؛ **غياب الملف ليس دليلًا على Retail**.
- Flags قابلة للملاحظة من PKG Header وحدها، ولا تثبت التوقيع، والتشفير، وتوافق Base/Patch، ومصدره القانوني أو حقوق تنزيله.
- قراءة Header لتطبيق مثبت لا تعني أن نسخة Base التي سيُجرى تحديثها تطابق PKG الأصلية، ولا تثبت نجاح التثبيت أو سلامته.
- لا يوجد دليل إذن عام لتنزيل FPKG patches تجارية. تبقى كل الواجهات مقفلة.

## بوابة جهاز حقيقي صغيرة وآمنة (مستقبلية)
1. بعد نجاح Cross Compile للمسار الاختياري، أنشئ **حزمة اختبار diagnostic-only** منفصلة بإعلام واضح وعدم منحها أي صلاحيات Install. لا تجعلها إصدار Release.
2. على PS4 14 مع GoldHEN، نفّذ الفحص على تطبيق Homebrew تملك حزمته الأصلية، لا على لعبة مدفوعة أو Saves. احفظ Log منفصلًا مع نتيجة `OPEN_FAILED` أو `SAFE_OPEN_UNAVAILABLE` أو `STAT_UNAVAILABLE` أو `FAKE_FLAG_CANDIDATE`.
3. قارن Flags المستخرجة مع **ملف PKG أصلي تملكه** وفُحص Offline؛ لا تقبل نجاح تصنيف لعبة واحدة حكمًا عامًا على Firmware/External Storage.
4. إذا فشل الوصول، أوقف هذا الفرع وارجع إلى عرض البيانات المحلية بدون كسر Scanner. لا تفتح التثبيت أو تعتمد على اختبار Emulator وحده.
5. بعد اختبار Base + Patch قانونيين، أضف طبقة تثبيت آمنة **منفصلة**، مع موافقة صريحة ونسخ احتياطي ونتائج BGFT/AppInstUtil حقيقية.

**الحد الفاصل:** `FAKE_FLAG_CANDIDATE` ليست `FPKG_VERIFIED`، و`SFO` ليست دليلًا على نوع التوقيع، ولا يوجد أي Download/Install في هذا التغيير.
