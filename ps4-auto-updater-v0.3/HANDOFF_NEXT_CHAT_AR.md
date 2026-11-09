# PS4 Auto Updater — Handoff للمحادثة التالية

> آخر تحديث: 2026-11-09 (Asia/Riyadh). هذا المستند هو نقطة تسليم مستمرة. **اقرأ حالة GitHub الحية قبل أي تعديل**؛ لا تفترض أن هذا الـSHA سيبقى الأحدث.

## 1. مهمة المشروع وسلوك المساعد

- المستخدم يريد تطبيق **PS4 Native PKG** يعمل على **Firmware 14.00 مع GoldHEN** (التوافق هدف **يجب اختباره** وليس أمرًا مثبتًا).
- الدور: Principal Software Engineer صارم، الرد **بالعربية مع المصطلحات التقنية English**، الأولوية للصحة والاعتمادية على حساب المجاملة.
- لا ترسل **PKG للمستخدم** حتى تكتمل الواجهة المعتمدة ويجتاز العمل كل اختبارات الكمبيوتر المتاحة. المستخدم هو من سيجرب التطبيق لاحقًا على PS4 الحقيقي؛ لا تدّع إجراء اختبار Hardware.
- **المرحلة الحالية: إكمال واجهة Royal Blue**. لا تخرج إلى وظائف Update Discovery/Download/Install قبل انتهاء عمل الواجهة واستقرارها وفحص جدوى كل ميزة بمصدر أصلي.
- المستخدم حسم شكل واجهة Royal Blue مع خلفية ضبابية لصورة اللعبة المحددة وتوازن بصري متوسط؛ الأهم أن يكون التطبيق خفيفًا. صور المرجع موجودة في المحادثات السابقة بالمشروع؛ اطلب رفعها إن لم تكن متاحة فعلًا.
- قرارات السلوك: عند عدم العثور على تحديث موثوق تظهر رسالة صريحة؛ الفحص يفتح Snapshot ثم يفحص دون تعطيل الواجهة؛ نقل البيانات بالخلفية فقط عندما يثبت دعم PS4؛ لا تبدأ التنزيل عند نقص المساحة، أظهر المطلوب؛ لا تحذف الملف إلا بعد تأكد نجاح تثبيت آمن؛ استكمال التحميل وإعادة المحاولة بحذر وبعد التحقق من المصدر/الملف؛ طلب المستخدم عند الخروج أثناء تنزيل فعلي (لم تُنفذ هذه الوظائف بعد).
- لا تخلط بين Runtime code written وHost tested وOpenOrbis compiled وPS4 tested. لا تدّع امتلاك مصادر تحديث Retail/FPKG أو تثبيت آمن.

## 2. مستودع GitHub ومكان الكود

- Repo: https://github.com/obsp081-coder/ps4-updates
- Branch التي نعمل عليها: `fix/scanner-cid-and-inventory-capacity`
- **آخر Commit كود تم التحقق منه قبل تحديث هذا الملف:** `1ed079bf4bdf09350da4b503c1cc2fbfb4bfd85c`؛ قد يوجد Commit توثيق أحدث منه. **استعلم عن HEAD الجديد قبل تعديل الملفات.**
- مسار المشروع: `ps4-auto-updater-v0.3/`
- وحدة الرسم الأساسية: `ps4/src/scanner_ui.cpp`، التنقل: `ps4/src/ui_navigation.c/h`، الفحص: `ps4/src/ftp_scanner_v05.c`، الأغلفة: `ps4/src/ui_icon_cache.c/h`، النص المختلط: `ps4/src/ui_title_layout.c/h`، SFO/Snapshot: `ps4/src/ui_snapshot.c/h`.
- بناء Atlas Arabic وقت Build: `ps4/tools/generate_ui_arabic.py`.
- حالة تقنية مفصلة يجب قراءتها: `ps4-auto-updater-v0.3/UI_IMPLEMENTATION_STATUS_AR.md`.
- تستات UI وملاحظة Screenshot: `tests/test_native_ui.py` و`tests/ui_mocks/ui_smoke.cpp`.
- CI الأخير الناجح **على كود Commit 1ed079bf**: https://github.com/obsp081-coder/ps4-updates/actions/runs/42598165602 — **181 Host tests PASS** و**OpenOrbis Native Build PASS**. CPU Host-only لـ12 Native 1080p Submissions: **25.728ms إجمالًا (2.144ms/submission)**، وDirect Memory للـFramebuffers **16 MiB**؛ ليس إجمالي RAM أو أداء PS4. اختبار Idle لا يعيد رسم الواجهة خلال 120 Polls بلا تغيير.
- ابحث عن GitHub Action الأحدث لكل Commit بعد تسليم هذا المستند ولا تعتمد نتائج Commit أقدم على تغييرات أحدث.

## 3. ما هو موجود فعليًا (ليس كل الوظائف المطلوبة)

- Native UI بعرض 1920×1080 عبر CPU framebuffer + VideoOut/Pad؛ 6 صفوف في قائمة الألعاب، Home، Details، Search English/Arabic مع فتح نتيجة، Downloads View-only، Settings View-only.
- التصميم Royal Blue، Header ثنائي اللغة، Hero Card للصورة المحلية، Details تعرض خانات الإصدار/التصنيف/المنطقة/الحجم/Firmware/المصدر وتكتب «غير متوفر» عند غياب الدليل، Downloads تعرض صفر Jobs الحقيقي بدل Progress مصطنع.
- قراءة Metadata و`param.sfo` للـBase Games عبر **GoldHEN FTP loopback read-only**؛ Scanner على Worker منفصل مع Cancel/Join، Snapshot سابق مع تحذير، حماية Snapshot صالح من فحص جزئي، Lazy Loading لأغلفة محلية عبر Worker مستقل وLRU لثماني صور.
- `metadata_verified` **لا تعني installed proof**. `Retail PKG` و`FPKG` ليست حزمًا قابلة للخلط تلقائيًا. لا يوجد Provider تحديثات مثبت الصلاحية أو Downloader/Installer آمن ومتحقق من عمله. **لا تفعّلها افتراضيًا**.
- البحث يدعم اختيار نتيجة حقيقية والانتقال إلى Details والعودة إلى البحث؛ الخروج عبر Circle/L1/R1/OPTIONS لا يحمل الفلتر المؤقت إلى Home، وكذلك فقدان CUSA أثناء Rescan يغلق Details ويرجع إلى المكتبة الكاملة. البحث العربي يستخدم تطبيعًا **محدودًا** لأشكال الألف والياء والتشكيل والتطويل دون مكتبات Unicode ثقيلة، ولا يدعي Unicode casefold كاملًا.
- `UI title layout` يحسّن العربية/Latin لكنه **ليس Unicode Bidi كاملًا**.
- آخر أعمال UI نفذت **Sidebar عربية 252px** و**جدول اللعبة / الإصدار / الحالة** بستة صفوف، وLong RTL/LTR Ellipsis؛ أُصلحت حالات Cached/Failed FTP وEmpty Library ومنع رسالة الحفظ الوهمية. **التحديث الأخير**: الخروج من Search لا يترك فلترًا مخفيًا في Home؛ يُستعاد عدد الألعاب ويُحفظ اختيار CUSA، كما تميز Details بين تحذير Cache أثناء الفحص والتحذير بعد فشل FTP. صور `search_restored_home.png` و`scanning_details.png` و`stale_details.png` اختُبرت بصريًا عبر Host. التصميم **غير مكتمل**؛ لا تكتب "UI finished".

## 4. الأولويات التالية — بدون انتظار أسئلة للمستخدم

1. **القائمة الجانبية العربية العريضة (DONE IN CODE + HOST TESTS):** 252px بديل Icon Rail القديم مع جميع أقسام الواجهة. **لم يُختبر على PS4 الحقيقي**؛ لا تعد بناءه من الصفر إلا عند وجود عيب موثق.
2. **Navigation/Search/Host 1080p baseline (DONE IN CODE + HOST TESTS):** ستة صفوف وجدول عربي وتحذيرات Cache/Empty آمنة، RTL/LTR ellipsis، إصلاح L1/R1/OPTIONS/Rebind، بحث عربي محدود. أُضيف قياس Host 1080p CPU وحجز 16 MiB لاثنين Framebuffers واختبار أن 120 Idle Pad Polls لا تسبب Repaint زائد. **أولوية P1 الحالية:** ضبط Contrast/Spacing والأغلفة، واختبار موارد وFlip/Pad/GoldHEN FTP على جهاز PS4 الحقيقي عند توفره؛ أي Native Host CPU benchmark ليس Hardware FPS أو Total RAM. لا تُعد الإصلاحات السابقة دون عيب مثبت.
3. تحسين التركيز البصري وNavigation states للـSearch/Details/Downloads/Settings، ولا تضف أزرار Download/Install فعالة أو Progress وهمية.
4. كل تغيير: اقرأ الملفات المعنية من GitHub، عدّل بحذر وبـCommit واضح، أضف Host Regression Tests، راجع صور `ui-native-host-previews` بصريًا، انتظر CI المكتمل على أحدث HEAD، وصحّح الفشل الفعلي قبل اعتماد التعديل.
5. قياس الموارد والذاكرة/الأداء على الكمبيوتر عند الإمكان مع تمييز أنها ليست أرقام PS4. PS4 Hardware Tests، FTP على GoldHEN FW14، الأعطال والتثبيت تؤجل حتى الجهاز متاح.

## 5. قواعد هندسية ملزمة

- **Verdict للمقترحات المهمة: ACCEPT/MODIFY/DEFER/REJECT** مع دليل أو Unknown؛ لا مسايرة.
- لا تستخدم بيانات أو مصادر تحديث غير مرخّصة. أي اختبار Download/Install يحتاج فحص Content ID/Title ID/Region/Version/Minimum FW/PKG vs FPKG والتوافق قبل السماح بأي فعل.
- لا تستخدم `sceAppInstUtil` أو خريطة API غير موثقة دون دليل وحدود واضحة. الحفاظ على مسارات read-only وExit الحالية.
- المستخدم لا يريد أن تتكرر أسئلة تصميم حُسمت بالفعل؛ استنتج الأفضل مع توثيق الافتراضات، ولا تَعِد بمواصلة عمل تلقائية أثناء غياب المستخدم.
- داخل ChatGPT الجديد: **الاعتماد الأول على حالة GitHub الحية** ثم هذا Handoff ثم محادثات المشروع/الصور المتوفرة بالفعل.

## 6. أول عمل في المحادثة التالية

افتح `UI_IMPLEMENTATION_STATUS_AR.md`، وافحص أحدث HEAD وCI و`scanner_ui.cpp`/`ui_navigation.c`. **Sidebar/3-column game table/RTL-LTR Ellipsis/Cached-Stale-Empty وSearch Navigation/Arabic Folding نُفذت واختُبرت على Host**؛ لا تعدها من الصفر بلا عيب مثبت. الأولوية هي ضبط التفاصيل البصرية وحالات البيانات الفعلية **والتحقق من Lazy Cover Worker وFlip/Pad وذاكرة PS4 الحقيقية**؛ قياسات Host CPU/Framebuffer أُنجزت بالفعل. راجع صور Native Host، شغّل Host Tests وOpenOrbis Build، ولا ترسل PKG قبل اكتمال شروط التسليم.
