# حالة تنفيذ واجهة PS4 Native — Royal Blue / Arabic

**المعيار:** FINAL_PRODUCT_SPEC_AR.md و APPROVED_UI_PERFORMANCE_AR.md.
**آخر تحديث:** 2026-11-09.
**Verdict:** MODIFY — تكامل الواجهة **غير مكتمل**. هذا المستند ليس تقرير اختبار PS4.
**سياسة التسليم:** لا إرسال PKG تجريبي للمستخدم إلى أن تكتمل الواجهة كما طلب؛ رغم أن CI يولد حزمة لأغراض فحص البنية محليًا.

## 9 نوفمبر 2026 — Stage 6: Smooth selected-game background and bounded focus glow

- **CODE:** [8cf2403b](https://github.com/obsp081-coder/ps4-updates/commit/8cf2403b2b09d09247ce4f90975ab22832914c00) replaces the hard 8x8 nearest-neighbor selected-icon backdrop with integer bilinear interpolation from the SAME already-darkened local 240×135 `blur_image`. Only one 240-pixel stack row is used (~960 bytes); no new heap, source file, network access, user game mutations, runtime font, shader or extra VideoOut buffers.
- **FOCUS:** [d8ac86a2](https://github.com/obsp081-coder/ps4-updates/commit/d8ac86a24ea6804f4f5b858b01fb4bd9f7d71bdc) adds a tiny deterministic cyan halo to ONLY the selected library row's perimeter, respecting 3px outer gutter and leaving the center/content pixels unchanged. Python 1080p native framebuffer regression asserts the actual row's soft focus rim; C++ mock tests validate bilinear red/blue middle points, uniform colors and no channel crossover.
- **VERIFICATION:** [CI #42644209000](https://github.com/obsp081-coder/ps4-updates/actions/runs/42644209000) finished successfully: **181/181 Host tests PASS, OpenOrbis Native Build PASS** at code SHA `d8ac86a24ea6804f4f5b858b01fb4bd9f7d71bdc`; associated [Auto Tests #42644209213](https://github.com/obsp081-coder/ps4-updates/actions/runs/42644209213) also PASS. Host instrument reports 16,777,216 bytes allocated for VideoOut framebuffers, unchanged.
- **PERFORMANCE CAVEAT:** Host instrumentation has fluctuated (prior 2c6fb072 ~4.611 ms/submission; 8cf2403b ~5.864; d8ac86a2 ~8.121); CI runners differ, measurements include other mock/control operations and **are not comparable PS4 FPS benchmarks**. Extra bilinear per-pixel CPU work is real and needs actual FW14 hardware timing. If it causes missed UI responsiveness on a PS4, optimize or gate it rather than claiming cost-free quality.
- **NOT ACHIEVED:** Visual parity with user-provided concept art (especially Nathan Drake full-screen photography) remains unproven; do not embed hardcoded copyrighted assets or invent update/download details. Hardware FW14 GoldHEN, real icon handling and the UI redraw latency on real PS4 remain **Needs real-hardware testing**. PKG for these changes was not published; PR remains Draft. The previous shadPS4 v01.14 package is obsolete.
- **NEXT:** Audit current native screenshots side by side with three reference images at 1920×1080, then verify on shadPS4 and PS4 hardware, keeping the downloader/installer disabled.

## 9 نوفمبر 2026 — Stage 3: RTL Home Table + Header Match

- **Status/Verdict: MODIFY (visual improvement, not 1:1 completion).** The user explicitly approved three PS4 Royal Blue visual reference screenshots; native Host captured `home.png`, `details.png`, `downloads.png` are generated from the exact C++ VideoOut framebuffer code, not a PS4 hardware session.
- **Home layout:** [611fa37c](https://github.com/obsp081-coder/ps4-updates/commit/611fa37c048e84423c7543b185e8f4e4a631a55f) moves game `icon0` to the far-right end of each of the six 88px rows, aligns game title/CUSA beside it, and adds FIVE truthful RTL columns: state, observed update size, observed update version, SFO metadata version (NOT necessarily installed), and game. Missing update/source fields show `--`; a model update candidate does not automatically authorize any download or installer. Local artwork remains square, not a made-up widescreen game cover.
- **Visual test corrections:** [a7511407](https://github.com/obsp081-coder/ps4-updates/commit/a75114077c86e27b875dc9c1bc73b7cb0849846b) and [ee066de1](https://github.com/obsp081-coder/ps4-updates/commit/ee066de18a3e92678566a8dad1ac3279ccf7bd5d) move real native pixel probes and Python screenshot assertions to the x922–1273 title field and add cover-gutter/RTL tests. Initial red CI was caused by a remaining old Python crop x377/x899, not a successful physical-console regression.
- **Masthead:** [a924e5e7](https://github.com/obsp081-coder/ps4-updates/commit/a924e5e787ee92a319fca916f6eb5f808186054d) increases the English white Noto text and replaces old PS4 block with a small native cyan controller glyph. It uses build-time-only Arabic labels at smaller sizes so the three-line heading fits 115px and keeps `READ ONLY` / `FW14 TARGET` badges truthful.
- **VERIFIED CI at code SHA a924e5e7:** [Auto Tests run 42629242209](https://github.com/obsp081-coder/ps4-updates/actions/runs/42629242209) **181/181 Host PASS** and [Native Build run 42629241983](https://github.com/obsp081-coder/ps4-updates/actions/runs/42629241983) **PC Simulation PASS + OpenOrbis PS4 Package Build PASS**. Native screenshots artifact `ui-native-host-previews` was downloaded locally for visual review. **NOT PS4 HW TESTED**; shadPS4 user's prior PKG v01.14 is older and does not include these edits.
- **Remaining visual gaps:** photo-like full-screen `Uncharted` artwork in concept is NOT shipped (no authorization and cannot be obtained from arbitrary devices); background is local `icon0` blurred only when available, otherwise dark gradient. The reference's green compatible status, internet online indicator, real update sizes, and download progress remain unavailable without authenticated source and compatibility verification. 8 actual Sidebar sections, Search via Triangle and auxiliary Details, `R1` from Settings -> Home wrap are maintained. GoldHEN FW14 real-device testing and performance/color checks still required. Downloader/installer disabled; PR remains DRAFT.
- **Next recommended visual work:** compare native 1080p screenshots against the three approved reference images, tune small visual metrics (spacing/glow/typography) without heavy effects or fake data, and then run a *new* test PKG on shadPS4 before physical-console safety tests. Do not declare full pixel match or gameplay update functionality.

## 9 نوفمبر 2026 — Stage 2: Layout Details وDownloads على مرجع Royal Blue

- **Actual code HEAD at checkpoint:** `e5aaed508e2f6ea8d4280075875a9e99a1ab10ce` (آخر تغيير Code قبل هذا التوثيق). Stage 1 Sidebar 8 real sections + R1/L1 wrap محفوظة، مع تبويب Settings في نهاية الدورة حتى يرجع Home مع R1. Full design match **not yet achieved**, no real FW14 hardware testing.
- **Details:** [9625eedc](https://github.com/obsp081-coder/ps4-updates/commit/9625eedcf91785fb7d5606e4f558548fbbec55cc) أعاد إنشاء ست بطاقات بيانات، شريط حالة التوافق **غير مؤكد**، صفوف النسخ المتاحة من UiUpdateInfo فقط، ثلاث أزرار رمادية **مقفلة**، وRight Panel محلية للعبة. [27b60140](https://github.com/obsp081-coder/ps4-updates/commit/27b60140b9dd9ca0217f01cd616cf18d4c3e4936) فصل تسمية المصدر عن قيمة «غير متوفر» وأُضيف Pixel Regression.
- **Downloads:** [30ef5f12](https://github.com/obsp081-coder/ps4-updates/commit/30ef5f1237d82495faed21aa3b7e27e39d188012) أعاد التوزيع إلى 4 Real KPIs، قائمة 5 صفوف من Jobs الحقيقية (إن وجدت)، يمينها معلومات المهمة المختارة، وشاشة No jobs صريحة. Progress لا يأتي إلا من `bytes_done/bytes_total` الفعلية، **لا Pause/Cancel أو Download حقيقي**.
- **Header:** [e5aaed50](https://github.com/obsp081-coder/ps4-updates/commit/e5aaed508e2f6ea8d4280075875a9e99a1ab10ce) استبدل 24px jagged line بقوس Quadratic 1px smooth، بدون Animation/Shader/Assets خارجية.
- **Verification:** [CI #42623027998](https://github.com/obsp081-coder/ps4-updates/actions/runs/42623027998) على Commit `108d6efcb16606e26012d954000dac0a810a73c3` أثبت 181/181 Host Tests + OpenOrbis Native Build PASS، ويشمل Native Host Screenshots Home/Details/Downloads. **الـCI على Commit القوس e5aaed50 (#42623676076) يجب التحقق من حالته بعد كتابة التوثيق**؛ لا تنسب نجاح Build جديد قبل اكتماله.
- **What remains:** مراجعة مطابقة التصميم 1:1 بمعيار ألوان وأحجام وخطوط مقارنة مرجع المستخدم (الواجهة ليست صورة Concept)، تحسين Full-screen background من Local icon0 فقط وفق المصادر المسموح بها، اختبار على shadPS4 بإصدار أحدث ثم جهاز GoldHEN FW14؛ لا تدّع أن Mocked Host screenshot تمثل PS4 hardware. Fake source/firmware/compatibility/download content forbidden. PR يبقى Draft، PKG لا يُرسل تلقائيًا.

## 9 نوفمبر 2026 — Stage 1: Sidebar ذات 8 أقسام + Royal Blue Glow

- **User instruction:** تطابق واجهة Home / Details / Downloads مع ثلاث صور Concept Royal Blue المعتمدة. التنفيذ الحالي مرحلة أولى، **ليس تطابق 1:1**؛ لقطات Concept تتضمن Uncharted artwork وأرقام تحديثات وتنزيلات افتراضية لا نستخدمها كحقائق.
- **NAV:** القسمان Details وSearch شاشتان فرعيتان، وليسا من الثمانية. ترتيب R1/L1 الفعلي: Home -> Games -> Available Updates -> Downloads -> Queue -> Installed Updates -> About -> Settings -> Home. انعكس ترتيب About وSettings بالـSidebar بالنسبة للصورة حتى يتحقق طلب المستخدم الصريح **Settings + R1 -> Home** مع مرور R1 على كل 8. L1 يعود في الاتجاه المعاكس. Games تعرض المكتبة نفسها، وCross يفتح Details ثم Circle يعود Games. Search عبر Triangle؛ اللغة تتغير بالمثلث فقط. تم اختبار 16 حركة R1/L1 على Host Pad/VideoOut بما فيها الحالات بدون ألعاب.
- **UI:** جميع أقسام Sidebar الثمانية تظهر بالعربي مع أيقونات هندسية، وتتوفر شاشات View-only لكل منها. Available Updates لا تزعم وجود Update provider موثوق، Queue تظهر فقط Model Jobs الفعلية، Installed Updates لا تزعم إثبات تثبيت patch من SFO، About يوضح القيود. Home فيها Banner الترحيب + 4 قيم حقيقية، خلفية gradient وfocus rim وحدود سماوية وHeader Arc بدون Animation أو ملفات خط مرفقة. صورتان Host Previews: Home وAvailable.
- **Code:** [4667b154 nav](https://github.com/obsp081-coder/ps4-updates/commit/4667b154f1539e645cfbe00a6e09a847ed1ab5ad), [d5a83779 new pages](https://github.com/obsp081-coder/ps4-updates/commit/d5a83779bacbc32b21d3bbaa71edfd5ed1bcee90), [90d4d83c glow](https://github.com/obsp081-coder/ps4-updates/commit/90d4d83c11bcc404ed8313a87c6ffd425f6670b5).
- **VERIFICATION:** [GitHub Actions CI #42620995592](https://github.com/obsp081-coder/ps4-updates/actions/runs/42620995592): **181/181 Host tests and OpenOrbis Native Build PASS** on commit 90d4d83c. Captures are native 1080p Host VideoOut framebuffer images, NOT real PS4 photos. No new package released; old shadPS4 PKG 01.14 lacks these changes.
- **NEXT:** Refine Details / Downloads cards and actual approved composition; do not invent Uncharted images, installed-patch status, compat verdict, progress, online flag or implementation of download/install. Real FW14 GoldHEN performance, color, FTP metadata, shoulder behavior and cleanup must be tested on device separately.

## تحديث 9 نوفمبر 2026 — تجربة المحاكي ومطابقة تصميم Royal Blue

- شغّل المستخدم حزمة v01.14 على shadPS4 v0.21.0، وصوّر Home وDownloads وSettings وSearch عربي/إنجليزي. هذه تجربة Emulator فعلية، وليست اختبارًا على PS4 Firmware 14.00 مع GoldHEN. ظهرت مكتبة فارغة وخطأ Scan واحد في Emulator؛ لا تعني فقدان الألعاب على جهاز PS4.
- أصلح [Commit 2ebba558](https://github.com/obsp081-coder/ps4-updates/commit/2ebba5589eda49f62d8a3c656d96438f91343b1a) مشكلة R1/L1: التدوير Home → Details → Downloads → Search → Settings → Home وبالعكس، حتى في مكتبة فارغة. R1/L1 لم يعودا يبدلان لوحة البحث؛ Triangle وحده يتولى تغيير اللغة. مغادرة Search تزيل الفلتر المؤقت، وفقدان CUSA أثناء Rescan لا يفتح لعبة بديلة بالخطأ.
- أضافت تغييرات 12014ae6 وe90c5a18 و4474d046 و55289856 خلفية Royal Blue Gradient بدل الخطوط الأفقية، List أعرض مع Hero Rail أضيق، Hint حقيقي لـR1/L1، بانر الترحيب، وأربع بطاقات بمقاييس حقيقية: Base Games وUnknown Install وScan Errors وActive Downloads. ستة صفوف باقية على Home؛ لا Fake Progress.
- [CI Run 42617470851](https://github.com/obsp081-coder/ps4-updates/actions/runs/42617470851): **181/181 Host Tests PASS وOpenOrbis Native Build PASS**. Host Previews هي Native framebuffer images وليست لقطات PS4.
- **الفرق المتبقي الكبير:** التصميم المرجعي يحتوي 8 قوائم Sidebar (الرئيسية، الألعاب، التحديثات المتاحة، قائمة التنزيل، الانتظار، التحديثات المثبتة، الإعدادات، حول البرنامج) بينما الكود الحالي 5 صفحات فقط. طلب المستخدم Settings/R1 يعود Home؛ توسعة عدد صفحات R1/L1 تستوجب احترام هذا الشرط. صور Uncharted وأرقام التحديثات والتنزيلات في Concept Art لا تصلح بيانات حقيقية أو Assets تلقائية داخل التطبيق. لا تزعم تطابقًا 1:1 أو FW14 Verified أو Downloader Enabled.

## تحسين Focus في Royal Blue وإصلاح تحديث Blurred Cover — 9 نوفمبر 2026

- **VISUAL:** تمت إضافة Cyan focus rims محدودة بالـRect على Sidebar النشطة وصف اللعبة المحدد، لتحسين Contrast نحو التصميم المعتمد، بدون Animation loop أو Shader أو تخصيصات Heap. فحص Pixel `(120,193)` و`(600,351)` يثبت إضاءة التحديد، و`(600,450)` يضمن بقاء الصف غير المحدد بدون Cyan.
- **ROOT CAUSE / FIX:** استبدال Icon PNG لنفس CUSA عبر `scanner_ui_offer_icon()` كان يترك `blurred_for` صالحًا ويعيد استخدام خلفية ضبابية قديمة. الإصلاح يلغي صلاحية Cache بعد Offer ناجح، ويعيد الحساب في أول Draw لاحق؛ Thumbnail worker كان صحيحًا أصلًا ولم يتغير.
- **REGRESSION:** اختبر Host VideoOut/Pad استبدال Thumbnail أخضر بـPNG أزرق بعد الـInitial Draw، وتأكد من اختلاف الخلفية عند `(1190,500)` قبل وبعد، دون Network أو PS4 فعلي. صور Native Host من Run 42608511066 فُحصت بصريًا، مع 6,938 بكسل متغيرًا في Home ضمن Focus Areas.
- **EVIDENCE:** [Commit d40a02168210](https://github.com/obsp081-coder/ps4-updates/commit/d40a02168210737ae1a5d1a8c51dfffc96694e16) و[CI Run 42608511066](https://github.com/obsp081-coder/ps4-updates/actions/runs/42608511066) — **181/181 Host Tests PASS** و**OpenOrbis Native Build PASS**. لا اختبار PS4 Firmware 14.00 ولا دليل 30FPS على الجهاز.

## إصلاح ثبات اختيار Search وتصحيح Metadata diagnostics — 9 نوفمبر 2026

- **ROOT CAUSE:** `ui_nav_rebind()` كان يعيد استعمال `search_result_focus` بعد زوال CUSA المحددة، فيمكن أن يصبح التركيز على أول نتيجة أخرى دون اختيار المستخدم. الجديد: يحتفظ بـQuery والـKeyboard، ولكنه يزيل Result Focus عند فقدان الهوية؛ إذا تغير ترتيب الصفوف وبقيت CUSA، يُحفظ الاختيار الصحيح.
- **LOG CORRECTION:** `ftp_scanner_v05.c` كان يسجل `Metadata records` بعدد `found` للألعاب الأساسية فقط، رغم حفظ `metadata_records` لكل Base/Patch/DLC. تم تعديل التشخيص إلى `metadata_found` مع Host Regression على 2 Base + 1 DLC.
- **HOST EVIDENCE:** [Commit abb6bfc43aad](https://github.com/obsp081-coder/ps4-updates/commit/abb6bfc43aad78427741b6cbdce9c01c03b35ae2) نجح بـ **181/181 Host Tests** و**OpenOrbis Native Build** في [Run 42605227571](https://github.com/obsp081-coder/ps4-updates/actions/runs/42605227571). الاختبارات الإضافية ضمن Smoke الحالي ولم تضف Test Method جديدة.
- **INTERPRETATION:** ظهور `gp` داخل Host UI Preview القديم مصدره Host fixture مختلط، وليس السلوك الإنتاجي الذي يمرر فقط Base `games[]`؛ لا نحتاج تغيير Scanner الإنتاجي لخلل غير مثبت.
- **LIMIT:** لا Hardware PS4 test لهذا الفرع، ولا مسار Download/Install مخوّل أو مُفعّل.

## إصلاح تداخل تسمية Details مع Sidebar — 9 نوفمبر 2026

- **FIXED / HOST VERIFIED:** في Native Host Preview قبل الإصلاح، وصلت تسمية «تفاصيل اللعبة» إلى الفاصل الأيمن من الـ252px Sidebar. قياس Pixel gutter `(232, 310, 249, 345)` على `home.png`/`details.png` أعطى **81/119 pixel ساطع** قبل التعديل، وصفرًا في الحالتين بعده.
- **IMPLEMENTATION:** أضيفت `UI_AR_RAIL_DETAILS` بتوليد 26px وقت Build إلى `generate_ui_arabic.py` مع إبقاء تسمية عنوان Details الأصلية بحجمها. `rail_label()` يستعمل العنصر الجديد للعمود الجانبي فقط. لا Font runtime ولا عمليات Scale إضافية لكل Frame.
- **REGRESSION:** `test_native_ui.py` يفحص عدم وجود نص داخل الفاصل على Home وDetails. Commit الكود [437fcdfde0b0](https://github.com/obsp081-coder/ps4-updates/commit/437fcdfde0b0a62422a9a170e44916b47c32d77b): **181/181 Host Tests ناجحة** و**OpenOrbis Native Build ناجح** على [GitHub Actions 42602304942](https://github.com/obsp081-coder/ps4-updates/actions/runs/42602304942).
- **LIMIT:** لا توجد تجربة PS4 حقيقية لهذا التعديل؛ بقية عناصر الواجهة ومطابقة الـMockup ما زالت تحتاج مراجعة، ولا تُفعل تنزيلات أو تثبيت.

## أجزاء نُفذت بالكود في فرع GitHub (ليست كلها مثبتة على PS4)

| الملف/الوحدة | ما تنفذه الآن | مستوى الدليل |
|---|---|---|
| ps4/src/scanner_ui.cpp | Royal Blue بــ252px Arabic Sidebar لكل الصفحات، Home تعرض ستة صفوف ثابتة ومعلومات CUSA، Hero Card، Details بست Metadata Tiles، Search Keyboard وتنزيلات وإعدادات Read-only؛ يحافظ على Snapshot وعمل Lazy Icon worker | Host framebuffer pixel assertions + OpenOrbis Native Build؛ **يحتاج PS4 hardware** |
| ps4/src/ui_snapshot.c/h | Cache ثنائي للعبة الأساسية بحد 256 سجلًا وCRC؛ سياسة جديدة تمنع الفحص الجزئي من استبدال Snapshot صالح سابق. أول فحص داخلي جزئي يمكنه تكوين Snapshot فقط عند غياب Snapshot صالح؛ وحتى Snapshot فارغ صحيح لا يُستبدل بنتيجة ناقصة | اختبار Host للتلف والتكرار والحفظ الجزئي والسياسة + Native Build |
| ps4/src/ui_models.c/h | نماذج عرض محدودة للتحديثات وJobs، تعرض Progress من أرقام تُزوّد إليها دون صلاحية تنزيل أو تثبيت | اختبارات Host + بناء OpenOrbis |
| ps4/src/ui_navigation.c/h | تنقل وبحث وKeyboard عربية/English بحد 256 سجلًا؛ إعادة ربط Inventory مع اختيار CUSA؛ فتح نتائج Search والتفاصيل؛ تنظيف الفلتر عند الخروج عبر Circle/L1/R1/OPTIONS وحتى بعد إزالة CUSA بالـRescan؛ Arabic folding محدود لأشكال الألف والياء والتشكيل دون Heap | اختبارات Navigation Host + 256-game Host search stress + Native Controller Smoke |
| ps4/src/ui_title_layout.c/h | تقسيم اسم اللعبة إلى LTR/RTL runs بحسب أول حرف واضح، مع حدود 191 codepoint وبدون Heap؛ معالجة الحركات وعلامات الترقيم كعناصر محايدة للاتجاه؛ **ليس UAX #9 الكامل** | اختبارات Host مستقلة + اختبارات Framebuffer |
| ps4/tools/generate_ui_arabic.py | نصوص عربية ثابتة بخط Noto وRAQM وقت Build؛ عرض Dynamic Arabic Presentation Forms مع `advance` مستقل عن الهوامش الشفافة حتى لا تتباعد الحروف؛ لا شحن ملفات Font إلى PS4 | Host Preview + Native Build |
| ps4/src/ui_image.c/h | PNG decoder مقيد بحجم الملف والأبعاد، 128×128 ARGB | اختبارات Host |
| ps4/src/ui_icon_cache.c/h | 8 صور LRU؛ استلام Thumbnail متحقق منه على UI Thread من Mailbox بعد اكتمال طلب الصورة؛ رفض حجم خاطئ مع عدم إفساد Cache | اختبارات Host |
| ps4/src/ui_png_transport.c/h | فحص CRC لكل جزء PNG، ومحاولة استعادة النقل FTP ASCII فقط إذا تطابقت CRCs | اختبارات Host |
| ps4/src/ftp_scanner_v05.c | GoldHEN FTP read-only؛ Scanner Worker وCover Worker منفصلان عن UI؛ Covers تستخدم PASV-first ولا ترسل TYPE I الذي قد يفسد بروتوكول GoldHEN؛ Anonymous Auth يُعاد ضبطه لكل اتصال؛ عند Cancel يُتجنب QUIT على قناة قد تكون غير متزامنة؛ تحقُّق صارم من أكواد FTP وMultiline Replies | Host Mock FTP + اختبارات بنية Cover Worker + Native Build؛ **لم يثبت Cover Worker على FW14 الحقيقي** |
| ps4/src/ps4_shell_exit.c | إنهاء عبر SystemService بدل libc exit | تم التحقق من الخروج على FW14 في الإصدار السابق v0.6.5، **وليس** هذا الفرع |

## واجهة المستخدم حسب اختياراتك

- أزرق ملكي Dark Mode، حركات خفيفة وأولوية للموارد. الخلفية الحالية مصنوعة من صورة اللعبة المحلية بعد Blur منخفض الدقة عند التغيير.
- ستة صفوف متوسطة بأسماء نسخ النظام وCUSA وأيقونة محلية متاحة، وPlaceholder صريح عند غياب الصورة.
- لوحة جانبية مع Summary ثم صفحة Full Details، مع منع Download/Install للمرشحات غير المؤكدة.
- صفحة Downloads تعرض صفر مهمة عندما لا يزوّدها أي Downloader فعلي؛ يمكنها **رسم سجلات View-only** مع Progress بالأرقام التي تُمرَّر لها. هذا ليس Downloader، واختبار الحالة غير الصفرية استعمل Host fixtures فقط. صفحة Settings أصبحت توضح أن التخزين للتحديثات **غير مفعّل** ولم يتم ربط مصدر تحديثات بعد، بدل الإيحاء بوجود مصدر فعلي.
- Cache يحمل بيانات ألعاب قديمة ويعرضها فورًا بعد تشغيل الواجهة **إذا وُلد ملف snapshot_v1.bin من فحص سابق ناجح**. لا يجلب صور الألعاب المحفوظة بعد؛ عند فقد البيانات الأحدث يُبقيها مع تحذير Cache.
- الشاشة الرئيسية/شاشة التفاصيل/قائمة التنزيلات لا تعتمد على WebView أو خادم PC.
- Arabic shaping ثابتة في عناصر UI. العناوين المختلطة تستخدم LTR/RTL runs حسب أول حرف واضح؛ تظهر النتائج في `mixed_ltr.png`/`mixed_rtl.png`. صُحح انهيار عرض الأسماء الطويلة إلى خانة فارغة، وأضيف Visual Ellipsis مع `long_ltr.png`/`long_rtl.png`. تم ضبط اتصال Dynamic Arabic glyphs للكلمات مثل «لعبة». **لا ندعي UAX #9 كاملًا أو دعم CJK.**
- لوحة مفاتيح داخلية بدل استدعاء PS4 IME غير مثبت على FW14. تعرض صفحة Search نتيجة لعبة حقيقية (اسم وCUSA وغلاف محلي إن توفر) أسفل لوحة المفاتيح، وليس عدد النتائج فقط. سهم أسفل من آخر صف يركز النتيجة، يمين/يسار لتغيير النتيجة، وإكس يفتح تفاصيلها مع العودة إلى البحث بالدائرة.

## قياس native 1080p وIdle UI — 9 نوفمبر 2026

- **HOST BENCHMARK ADDED:** في `tests/ui_mocks/ui_smoke.cpp` شغّلت واجهة Native نفسها بدقة 1920×1080 عبر `VideoOut/Pad` mocks دون كتابة الصور على القرص أثناء القياس. مثال من GitHub Actions `42598165602`: **25.728ms Host CPU إجمالًا عبر 12 Frame Submissions (2.144ms/submission)**. يشمل Pad mock والتنقل، لذلك **ليس Render-only time ولا Hardware PS4 FPS**، ولا يوضع حد زمن Pass/Fail مع اختلاف مواصفات GitHub Runners.
- **MEMORY CHECK:** حجم Direct Memory المحجوز لاثنين من Framebuffers بدقة 1080p مع محاذاة 2 MiB هو **16,777,216 bytes = 16 MiB**. هذا حجز Framebuffers **فقط** وليس كامل استهلاك RAM؛ صور Cache وPNG Decoder وWorker stacks ونصوص الواجهة وغيرها إضافية.
- **IDLE REGRESSION:** اختبار جديد يؤكد أن **120 Pad Polls دون أزرار أو وصول Cover جديدة تؤدي إلى Frame Submission واحدة فقط** (العرض الأول)، ولا يعيد رسم 1080p كل دورة Polling.
- **VERIFIED:** آخر Commit كود `1ed079bf4bdf09350da4b503c1cc2fbfb4bfd85c` اجتاز **181 Host Tests** و**OpenOrbis Native Build** في [run 42598165602](https://github.com/obsp081-coder/ps4-updates/actions/runs/42598165602).
- **ENGINEERING VERDICT: MODIFY:** تجنب تعديل Renderer منخفض المستوى لتسريع تخميني ما دام Baseline Host ليس عائقًا مثبتًا؛ الأولوية لمراجعة سلوك Flip/Memory teardown وLazy FTP cover worker على Hardware حقيقي، ثم مقارنة التصميم بالمرجع. لا إرسال PKG حتى استيفاء الشروط.

## توحيد فلاتر البحث وتحسين Arabic Search — 9 نوفمبر 2026

- **FIXED IN CODE:** من صفحة Details المفتوحة عبر Search، الضغط على L1 للعودة إلى Home أو R1/Square للذهاب إلى Downloads لم يعد يترك Query قديمًا يخفي ألعاب Home. يحتفظ `ui_nav_action` بالـCUSA المحددة ويعيد المكتبة الكاملة. إذا أزال Rescan اللعبة أثناء Details، يغلق صفحة اللعبة ويزيل فلتر البحث القديم بدل إظهار مكتبة خالية كاذبة.
- **FIXED IN CODE:** `ui_navigation.c` يبحث باستخدام Limited Unicode Folding خفيف بلا Heap، للـASCII case-insensitive وبضع صيغ عربية: (أ/إ/آ/ٱ ↔ ا)، (ى ↔ ي)، مع تجاهل التشكيل والتطويل أثناء المطابقة فقط؛ أسماء الألعاب الأصلية لا تتغير. **لا ندعي Full Unicode Normalization/Casefold ولا بحثًا دلاليًا**.
- **HOST TESTED:** `navigation_smoke.c` يغطي L1/R1 بعد Details-from-Search وغياب الـCUSA في Rebind وتطابق Arabic variants. اختبار ضغط 128 عملية بحث على 256 اسم لعبة عربي طويل من بيانات صناعية؛ سُجلت **50.669ms CPU إجماليًا (~0.396ms للعملية)** على Host GitHub Actions واحد. هذا ليس متوسطًا معتمدًا ولا أداء PS4، ولا تستخدم أزمنة CPU كبوابة Pass/Fail.
- **EVIDENCE:** Commit الكود `54294a55a41c2454a1f82ceeaeb2ec512a20f577`: **181/181 Host Tests + OpenOrbis Native Build ناجحة** في GitHub Actions run `42596513513`.
- **OPEN:** Hardware PS4 FW14/GoldHEN ما زال غير مختبر؛ تحديثات الألعاب الحقيقية وDownload/Install غير متاحة؛ يلزم اختبار ضغط UI والأغلفة ومراجعة التعارض البصري مع المرجع.

## إصلاحات تنقل البحث وتحذير تفاصيل اللعبة — 9 نوفمبر 2026

- **FIXED IN CODE:** الخروج من Search إلى Home لم يعد يترك المكتبة مفلترة عندما تكون نتائج البحث صفرًا؛ يعود العدد الكامل من سجلات Scan الحقيقية، مع الاحتفاظ باختيار CUSA عند وجود نتيجة. زر OPTIONS من Search أو من Details التي فُتحت عبر البحث ينهي الفلتر المؤقت أيضًا. لا يجري أي تحديث أو تثبيت.
- **FIXED IN CODE:** صفحة Details تميّز بين حالة عرض Cache أثناء فحص FTP الجاري (`CACHE_PREVIEW`) وبيانات Cache بعد فشل أو عدم اكتمال الفحص (`CACHE_STALE`). لا تختلق حالة تحقق بعد انتهاء الفحص.
- **HOST REGRESSION:** اختبارات `navigation_smoke.c` تغطي البحث الفارغ، الرجوع بنتيجة محددة، والخروج عبر OPTIONS. واختبار `ui_smoke.cpp` ينفذ Pad Flow كاملًا: Search ثم حرف عربي لا يطابق لعبة، Circle للعودة إلى Home ثم Cross لفتح Details من مكتبة صحيحة. الصور `search_restored_home.png` و`scanning_details.png` و`stale_details.png` من Framebuffer الأصلي على Host، وليست من PS4.
- **HOST+BUILD EVIDENCE:** آخر Commit كود `a43c6785644b498da93ddbf9d88d7e57e6b2fac9`: **181/181 Host Tests ناجحة** و**OpenOrbis Native Build ناجح**، [GitHub Actions run 42593553873](https://github.com/obsp081-coder/ps4-updates/actions/runs/42593553873).
- **الحدود:** بحث العربية لا يدعم Unicode normalization/case-fold الكامل، ولا نعتبر نتيجة نجاح Host إثباتًا على GoldHEN Firmware 14.00.

## حماية حالات Cache والمكتبة الفارغة — 9 نوفمبر 2026

- **FIXED IN CODE:** نافذة تحذير Snapshot المحفوظ كانت تَحجب زر تفاصيل اللعبة داخل Home عند انقطاع FTP؛ صار الشريط في الهامش تحت **الصف السادس** وقبل Footer، مع الحفاظ على زر `التفاصيل الكاملة` وبدون أي تغيير على قراءة الألعاب أو حالة التثبيت.
- **FIXED IN CODE:** أُلغِي التحذير العربي ذو السطرين لأنه كان يقطع السطر الثاني داخل Footer. يوجد نص واحد للفحص الجاري (`CACHE_PREVIEW`) ونص مختلف لفشل/نقص الفحص (`CACHE_STALE`)، فلا تقول الواجهة «جاري التحقق» بعد انتهاء الفحص غير المكتمل.
- **FIXED IN CODE:** عند عدم وجود أي لعبة مرصودة تظهر لوحة Empty Library بالعربية وإرشاد مختصر لفحص خدمة GoldHEN FTP بدل ترك قائمة فارغة، ولا تُعرض تفاصيل لعبة وهمية أو حالة Download/Install.
- **FIXED IN CODE:** رسالة `تعذر حفظ قائمة الألعاب` لا تُعرض على نتيجة صفر لعبة أو فحص ناقص؛ لا تظهر إلا في حالة فحص مكتمل مع بيانات فعلية ولم تُحفَظ، بدل الإيحاء الكاذب بخطأ حفظ.
- **HOST TESTED:** Native VideoOut screenshots منفصلة: `cached.png` للفحص الجاري، `stale.png` للفحص الفاشل، `empty.png` للمكتبة الفارغة. Regression Checks تتحقق من بقاء ستة صفوف، وعدم إخفاء Details، وعدم وصول نص التحذير إلى Footer، واختلاف نصوص الحالتين ومنع رسالة الحفظ الوهمية. راجعت الصور بصريًا.
- **EVIDENCE:** آخر Commit كود `fb0d3c596bccb81a5b3494954a2ea61644e35680` اجتاز **181/181 Host Tests** و**OpenOrbis Native Build** في GitHub Actions run `42587102545`.
- **BLOCKER:** جودة صور Host ليست Hardware Test؛ أي FPS/ذاكرة/GoldHEN 14.00 ما زال يحتاج جهاز PS4 حقيقي. Update Catalog/Download/Install غير مفعلة.

## معالجة العناوين الطويلة والهيدر — 9 نوفمبر 2026

- **FIXED IN CODE:** `ui_title()` لم يعد يتخلّى عن الاسم بكامله عندما يكون مقطع Latin أو Arabic أكبر من عرض العمود؛ يقيس عرض كل Glyph من الـBuild-time Atlas ثم يعرض Prefix مرئيًا مع `...` عند النهاية البصرية (يمينًا لـLTR ويسارًا لـRTL). لا تخصيص Heap ولا فصل غير آمن لأحرف UTF-8.
- **HOST TESTED:** Fixture لاسم English طويل واسم عربي طويل في عمود 418px، مع فحص وجود الرسم عند طرفي الاسم وعدم تسرب أي Pixels بيضاء إلى عمود Version، وتصدير `long_ltr.png` و`long_rtl.png` من Native Host Framebuffer. راجعت الصور بصريًا؛ ليست صور PS4 حقيقية.
- **FIXED IN CODE:** إزالة 4 تبويبات مكررة من أعلى الواجهة بعد اكتمال Sidebar العربية العريضة؛ الهيدر يعرض `READ ONLY` و`FW14 TARGET` **كتصنيف هدف** دون ادعاء Online أو نجاح الاختبار على الجهاز. انتقلت اختبارات تنقل Pad وCapture Screenshots إلى حالة Sidebar النشطة نفسها.
- **VERIFIED HOST/BUILD:** آخر Commit كود `38bab300675071fe5f8c89746be3eff438257ce4`: **181/181 Host Tests** و**OpenOrbis Native Build** ناجحان عبر GitHub Actions `42582714894`.
- **حدود الحل:** اختصار الاسم ليس Unicode Bidi كاملًا ولا يعرض جميع خطوط CJK/Indic/Emoji. اختبار GPU/CPU/RAM وGoldHEN FTP والتوافق الفعلي مع Firmware 14.00 لا يزال يحتاج Hardware حقيقي. لا توجد وظائف تحديث/تحميل/تثبيت مخولة.

## Sidebar عربي عريض — 9 نوفمبر 2026

- **تم في الكود:** استبدال الـ69px Icon Rail بقائمة 252px تضم خمسة أقسام وظيفية فقط (Home/Details/Downloads/Search/Settings) مع تسميات عربية مشتقة من Build-time Atlas وHighlight واضح ووسم READ ONLY كامل.
- **تم في الكود:** نقل قائمة الألعاب وبطاقات الإحصاءات، وتوزيع Metadata في Details، ونقل Downloads/Settings ولوحة مفاتيح Search وشريط نتيجة البحث، وشاشة scanning، حتى لا تنزل العناصر تحت Sidebar أو تفقد صفوف الألعاب الستة.
- **تم في الكود:** اختصار حالة install unknown في الصفوف دون أن تزعم حالة Installed؛ ما زالت خانة الإصدار من Metadata ولا تعني توافق تحديث.
- **اختبارات:** Native Host frame captures لـHome/Details/Downloads/Search/Settings وCached، واختبار ظهور نصوص القوائم الخمسة وألوان Active Rail/Background وإحداثيات عناصر البحث والتنزيلات. آخر Commit كود ناجح: `fa4acc87bd3d80b17262bb3a7e4324e067822a7e`. **181/181 Host Tests وOpenOrbis Native Build ناجحان**: GitHub Actions run `42576271358`.
- **حد الدليل:** صور Host ناتجة عن Renderer الأصلي مع Mock VideoOut/Pad؛ لم تُختبر القائمة أو الذاكرة أو Controller navigation على PS4 حقيقي.
- **تحديث لاحق في نفس اليوم:** أُضيفت ثلاثة أعمدة عربية أساسية في Home: اللعبة والإصدار والحالة؛ بقيت الصفوف الستة وعدّلت إحداثيات النصوص وCUSA وحالة التثبيت غير المؤكدة، مع عدم إظهار أعمدة Updates غير موجودة.
- **اختبار مستقل:** Commit `a3c80fac0ab7f73a5349518188c380aee5b38533` اجتاز **181/181 Host Tests** و**OpenOrbis Native Build** على GitHub Actions run `42577043953`. صورة `home.png` من Native Host Preview تُظهر أعمدة Name/Version/State بوضوح، لكن ليست PS4 screenshot.
- **أولوية الواجهة التالية:** مراجعة أبعاد الخطوط والتباعد/العناوين مقابل المرجع وتجربة RTL متنوعة وحالات القائمة الفارغة والممتلئة، ثم قياس CPU/RAM على الجهاز عند توفره.

## تحسينات مرئية منفذة — 9 نوفمبر 2026

- **Home:** تم تكبير المعاينة الخاصة باللعبة المختارة إلى بطاقة Royal Blue ذات إطار للغلاف `icon0.png` المحلي، الاسم وCUSA والإصدار وحالة البيانات، وزر انتقال إلى تفاصيل **للقراءة فقط**. يظل غياب الغلاف Placeholder واضحًا؛ لا صور أو تحديثات مختلقة.
- **Masthead:** أصبح اسم `PS4 GAME UPDATE MANAGER` كاملًا فوق العنوان العربي، دون اقتطاع أحرف الخط التناسبي.
- **Details:** إعادة تصميم الصفحة إلى عمود معلومات بست بطاقات مستقلة (الإصدار، التصنيف، المنطقة، الحجم، Firmware، المصدر) وعمود غلاف منفصل. تعرض العناصر التي لا نملك دليلها `غير متوفر` ولا تستنتج Retail/FPKG أو توافق تحديث من `param.sfo`.
- **Downloads:** علاج تزاحم عنوان الصفحة مع بطاقات المؤشرات، وتحسين حالة صفر Jobs إلى لوحة حالة صريحة توضح غياب المصدر الموثوق وعدم تشغيل التثبيت، من دون نسب Progress وهمية.
- **Settings:** إشارة `READ ONLY` واضحة فوق الخيارات الحالية للعرض فقط؛ لا تبدو وكأنها تغيّر تفضيلات محفوظة أو تفعّل التنزيلات.
- **اختبارات التصميم:** أُضيفت نقاط تحقق Pixel على Framebuffer الأصلي لبطاقة Home وبطاقات Details وتحذيرات Downloads، مع بقاء اختبارات التنقل وJobs والبحث وكامل القائمة السابقة. لقطة UI عبر Host لا تثبت تنفيذ PS4.
- **الدليل:** على Commit `e98f1b57` نجح **181 اختبار Host** ونجح **Native Build** عبر GitHub Actions run `42572121599`، وتحققنا بصريًا من لقطات Home وDetails وDownloads وSettings.

**ما زال مختلفًا عن التصميم المعتمد:** القائمة الجانبية العربية العريضة وأعمدة Name/Version/State الأساسية أصبحت منفذة ومختبرة على Host، لكن التطابق الكامل مع ترتيب أعمدة المرجع والتدرجات والأغلفة لم يثبت بعد؛ ليس لدينا بيانات Update حقيقية أو Jobs تشغيلية أو إثبات FPS أو سلوك FW14. لا تُمثَّل الحقول غير الموثوقة كأرقام أو حالات Compatibility مصطنعة. هذه الفجوات تمنع اعتماد الواجهة كمنتج نهائي.

## نواقص الواجهة قبل اعتبارها كاملة

1. **Background Worker للـScanner:** تم فصل FTP وSFO وصور الألعاب عن VideoOut/Pad عبر `scePthreadCreate` في الكود، مع `scePthreadJoin` وCancel متعاون وإعادة عرض Snapshot أثناء الفحص. اختبارات Host تغطي التنقل إلى التفاصيل والإعدادات والعودة أثناء انتظار Worker، واختبار الخروج قبل اكتمال Worker. **مع ذلك: لا توجد حتى الآن تجربة حقيقية تثبت توافق API/استقرار الواجهة على Firmware 14.00.** عند فشل إنشاء Worker لا يتم إجراء فحص متزامن يجمّد الواجهة، بل تعرض حالة Cache غير مؤكدة. أول تشغيل دون Snapshot يبدأ بقائمة فارغة مؤقتًا؛ نقل بيانات JSON القديمة غير منفذ.
2. **صور الأغلفة:** أزيل Prefetch الثابت لأول ثماني ألعاب؛ يوجد الآن Lazy Loading عند اختيار اللعبة/التمرير عبر Worker منفصل وMailbox وتحقق CRC وPNG، مع Cache لثماني صور. أُصلح إرسال TYPE I غير المدعوم على بعض GoldHEN FTP Servers، وأُعيد تعيين Anonymous Auth لكل اتصال. اختبارات Host تثبت أن UI يطلب غلاف لعبة بعد الثامنة ويعيد الرسم بعد وصول صورة متأخرة؛ واختبارات بنية المصدر تتحقق من PASV-first والقراءة فقط. **لكن التعامل الفعلي مع GoldHEN FTP على FW14 غير مختبر بعد**؛ الصور غير محفوظة على القرص من Snapshot، ولا توجد Retry Policy مستقلة للأغلفة غير المتاحة، والبحث لا يحمّل أغلفة أثناء لوحة المفاتيح.
3. **عناوين الألعاب بلغات مختلطة:** تحسن أول مقطع عربي/Latin مع اتجاه حسب أول حرف واضح، وتم اختبار لقطتين من Native Host Framebuffer؛ لكنه **ليس Unicode Bidi كاملًا** ولا يطبع الحركات العربية فوق الحروف ولا يدعم أسماء Asian/Indic/Emoji بشكل صحيح. ما زلنا نحتاج اختبارات نصوص فعلية متنوعة وعلاجًا ثابتًا أو Renderer أوسع بعد قياس أثره على RAM/CPU.
4. **القياس الدقيق:** عرض حجم لعبة/منطقة/نوع Retail أو FPKG/النسخة ومتطلبات FW لا يُملأ إلا من مصادر مؤكدة، ولا يجوز استنتاجه من SFO وحده. حاليًا تظهر «غير متوفر» لهذه الخانات.
5. **جودة مطابقة التصميم المعتمد:** الأيقونات والحدود وخط العناوين والتدرجات وتوقيت الحركة، وتناسق RTL في شاشة 1080p على جهاز فعلي؛ ما زالت بحاجة مراجعة بصرية ومقارنة بالـMockups.
6. **التنقل/الأداء:** تم تغيير Polling للـPad من 100ms إلى ~33ms دون إعادة رسم كل Frame، لكن **30 FPS ليس مثبتًا**. أُضيف Host Native Framebuffer test لمكتبة فارغة و256 سجلًا وحالة Count أكبر من الحد، مع فحص تحرير موارد VideoOut. العدد المعروض يُحصر ضمن 256. ما زلنا نحتاج قياس التوقيت وRAM على PS4 الأساسي مع 0 و256 لعبة، واختبار 100 فتح/خروج، وانقطاع FTP، وPNG تالف/كبير والتراجع عند فشل إنشاء Cover Worker.
7. **الإشعارات والصوت:** لا تُفعَّل حتى الحصول على واجهات PS4 Native مثبتة؛ طلب المستخدم أصواتًا فقط عند اكتمال التنزيل أو حدوث خطأ.
8. **بيانات تنزيلات حقيقية/تحذير الخروج:** صفحة Downloads الأساسية جاهزة بصريًا لكنها لا تتعامل مع Jobs حقيقية؛ ربط Queue لاحقًا لن يُعرض كمكتمل قبل إثبات تنزيل/Resume/Install على FW14. التنقل من Search إلى Details صار يعمل بالكود واختبارات Host، لكنه يحتاج اختبار يد التحكم الفعلية وقياس RTL على التلفاز.


## تدقيق ثبات المكتبة وGoldHEN FTP — 2026-11-08

- **FIXED IN CODE:** فشل LIST الخارجي لم يعد يمحو Snapshot سابقًا صالحًا ببيانات داخلية جزئية؛ يسمح بحفظ نتيجة جزئية فقط عند عدم وجود Cache صالح، ويظل العرض يصرح بأن البيانات غير مؤكدة.
- **FIXED IN CODE:** أمر TYPE I أُزيل من Cover Worker واستخدم نفس مسار PASV-first الخاص بالـScanner؛ هذا علاج خطأ بروتوكول متوقع من ملاحظات GoldHEN السابقة، وليس إثبات تشغيل الصور على الجهاز.
- **FIXED IN CODE:** منع مشاركة قرار Anonymous Auth بين اتصالي الفحص والأغلفة؛ وعند Cancel لا يُرسل QUIT على قناة FTP بعد نقل غير مكتمل.
- **FIXED IN CODE:** اشتراط أن يكون رد FTP من ثلاثة أرقام يتبعها مسافة أو شرطة، وتجاهل الأسطر القصيرة داخل Multiline Replies بدل قراءتها ببايتات قديمة.
- **HOST VERIFICATION:** اختبارات Snapshot Policy، وMock FTP، وفحص Contract لبنية Cover Worker. فحص Contract لا يشغّل PS4 APIs؛ نجاحه وحده ليس إثباتًا لسلوك FTP على Firmware 14.00.
- **OPEN BLOCKER:** اختبار فتح التطبيق مع مكتبة كاملة ثم فصل وسائط التخزين الخارجية، وتكرار Cancel أثناء نقل PNG وعند إنهاء التطبيق على جهاز PS4 حقيقي دون المساس بالملفات أو المحفوظات.

## نقاط الأمان
- لا تثبيت ولا تنزيل غير مصرّح؛ لا تحميل صورة من الإنترنت.
- إن كان FTP في وضع ASCII، لا يمر PNG إلى Decoder إلا بعد استعادة محتواه ونجاح CRC لكل chunk.
- كل هوية CUSA تثبت من Metadata معقولة، لكن ذلك لا يثبت حالة Installed.
- تجنب تغيير منطق GoldHEN read-only وSystemService exit الذي نجح سابقًا.
- يجب إعادة الاختبار على PS4 FW14 لتشغيل UI الجديد؛ نجاح Host/CI لا يكفي.

**التحقق من هذه المرحلة (2026-11-08):** اختبارات Host تتضمن Rebind يحافظ على الهوية عند إعادة ترتيب الألعاب، واختبار Lazy Cover Mock يرسم الصورة عند وصولها واختبار طلب صورة بعد اللعبة الثامنة؛ راجع نتيجة CI على **آخر Commit** قبل اعتماد البناء. نجاح OpenOrbis لا يثبت اتصال Cover Worker على FW14 ولا FPS ولا استقرار التشغيل الحقيقي. لا يوجد اختبار Hardware لهذه التغييرات بعد.

**ملفات اختبار Host:** test_cover_ftp_safety.py وtest_ftp_scanner_v05.py وtest_native_ui.py وtest_ui_settings_truth.py وtest_ui_title_layout.py وtest_ui_navigation.py وtest_ui_image.py وtest_ui_icon_cache.py وtest_ui_png_transport.py وtest_ui_models.py وtest_ui_snapshot.py. لقطات `mixed_ltr` و`mixed_rtl` مسجلة من Host Framebuffer فقط وليست من PS4.
