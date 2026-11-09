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
- **آخر Commit كود تم التحقق منه قبل تحديث هذا الملف:** `552898562719f0821349447dbc8d96593cd82010`؛ قد يوجد Commit توثيق أحدث منه. **استعلم عن HEAD الجديد قبل تعديل الملفات.**
- مسار المشروع: `ps4-auto-updater-v0.3/`
- وحدة الرسم الأساسية: `ps4/src/scanner_ui.cpp`، التنقل: `ps4/src/ui_navigation.c/h`، الفحص: `ps4/src/ftp_scanner_v05.c`، الأغلفة: `ps4/src/ui_icon_cache.c/h`، النص المختلط: `ps4/src/ui_title_layout.c/h`، SFO/Snapshot: `ps4/src/ui_snapshot.c/h`.
- بناء Atlas Arabic وقت Build: `ps4/tools/generate_ui_arabic.py`.
- حالة تقنية مفصلة يجب قراءتها: `ps4-auto-updater-v0.3/UI_IMPLEMENTATION_STATUS_AR.md`.
- تستات UI وملاحظة Screenshot: `tests/test_native_ui.py` و`tests/ui_mocks/ui_smoke.cpp`.
- CI الأخير الناجح **على كود Commit 552898562719**: https://github.com/obsp081-coder/ps4-updates/actions/runs/42617470851 — **181/181 Host Tests PASS** و**OpenOrbis Native Build PASS**، دون Hardware test. فحص Native Host Preview للـSidebar موثق في Run 42602304942. قياس الأداء السابق على Commit 1ed079bf/run 42598165602 بلغ **25.728ms إجمالًا لـ12 Host Submissions** و**16 MiB** حجز Framebuffers؛ لا ننسب الرقم بالضرورة للبناء الأحدث ولا نعدّه FPS أو Total RAM لجهاز PS4. لا يكرر Idle Poll redraw غير ضروري.
- ابحث عن GitHub Action الأحدث لكل Commit بعد تسليم هذا المستند ولا تعتمد نتائج Commit أقدم على تغييرات أحدث.

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

## 3. ما هو موجود فعليًا (ليس كل الوظائف المطلوبة)

- Native UI بعرض 1920×1080 عبر CPU framebuffer + VideoOut/Pad؛ 6 صفوف في قائمة الألعاب، Home، Details، Search English/Arabic مع فتح نتيجة، Downloads View-only، Settings View-only.
- التصميم Royal Blue، Header ثنائي اللغة، Hero Card للصورة المحلية، Details تعرض خانات الإصدار/التصنيف/المنطقة/الحجم/Firmware/المصدر وتكتب «غير متوفر» عند غياب الدليل، Downloads تعرض صفر Jobs الحقيقي بدل Progress مصطنع.
- قراءة Metadata و`param.sfo` للـBase Games عبر **GoldHEN FTP loopback read-only**؛ Scanner على Worker منفصل مع Cancel/Join، Snapshot سابق مع تحذير، حماية Snapshot صالح من فحص جزئي، Lazy Loading لأغلفة محلية عبر Worker مستقل وLRU لثماني صور.
- `metadata_verified` **لا تعني installed proof**. `Retail PKG` و`FPKG` ليست حزمًا قابلة للخلط تلقائيًا. لا يوجد Provider تحديثات مثبت الصلاحية أو Downloader/Installer آمن ومتحقق من عمله. **لا تفعّلها افتراضيًا**.
- البحث يدعم اختيار نتيجة حقيقية والانتقال إلى Details والعودة إلى البحث؛ الخروج عبر Circle/L1/R1/OPTIONS لا يحمل الفلتر المؤقت إلى Home، وكذلك فقدان CUSA أثناء Rescan يغلق Details ويرجع إلى المكتبة الكاملة. البحث العربي يستخدم تطبيعًا **محدودًا** لأشكال الألف والياء والتشكيل والتطويل دون مكتبات Unicode ثقيلة، ولا يدعي Unicode casefold كاملًا.
- `UI title layout` يحسّن العربية/Latin لكنه **ليس Unicode Bidi كاملًا**.
- أحدث إصلاح بصري في `437fcdfde0b0`: عنوان «تفاصيل اللعبة» في Sidebar كان يصل إلى الفاصل (81 pixel ساطع في Home و119 في Details ضمن x=232..248)، وأُضيف Atlas label مستقل بحجم خط 26px بدل تغيير عناوين Details الأخرى؛ بعد الإصلاح العدد 0 في الصورتين. Regression Test ضمن 181 اختبارًا وNative Build ناجح؛ لم يُختبر على PS4 حقيقي.
- آخر أعمال UI نفذت **Sidebar عربية 252px** و**جدول اللعبة / الإصدار / الحالة** بستة صفوف، وLong RTL/LTR Ellipsis؛ أُصلحت حالات Cached/Failed FTP وEmpty Library ومنع رسالة الحفظ الوهمية. **التحديث الأخير**: الخروج من Search لا يترك فلترًا مخفيًا في Home؛ يُستعاد عدد الألعاب ويُحفظ اختيار CUSA، كما تميز Details بين تحذير Cache أثناء الفحص والتحذير بعد فشل FTP. صور `search_restored_home.png` و`scanning_details.png` و`stale_details.png` اختُبرت بصريًا عبر Host. التصميم **غير مكتمل**؛ لا تكتب "UI finished".

### تشطيب 2026-11-09 — Royal Blue focus وselected-cover blur refresh

- **Code:** `d40a02168210737ae1a5d1a8c51dfffc96694e16` ([CI](https://github.com/obsp081-coder/ps4-updates/actions/runs/42608511066)). Sidebar النشطة وصف اللعبة المحدد أصبح لهما Cyan focus rim من ضربات Rect صغيرة، بلا Animation/GPU/Runtime Allocations. فحوص `test_native_ui.py` تثبت مواضع التحديد وتمنع تمييز صف غير محدد.
- **Fix:** `scanner_ui_offer_icon()` كان يستبدل PNG داخل Icon Cache دون إبطال `blurred_for` عند ثبات CUSA، فيُعاد رسم خلفية اللعبة القديمة. الآن يبطل Cache بعد `offer` ناجح فقط وعلى UI thread. يختبر `ui_smoke.cpp` إدراج Thumbnail أخضر، ثم استبداله بـPNG أزرق لنفس ID، ثم ظهور لوني Backdrop الصحيحين من Framebuffer الحقيقي الخاص باختبارات Host.
- **Visual evidence:** صورة `home.png` من `ui-native-host-previews` في Run 42608511066؛ قياس محلي قبل/بعد 6,938 Pixel مختلفًا ضمن Focus Areas، والمرتكزات غير المعنية ثابتة. **هذا ليس اختبار شاشة PS4**.
- **Pass:** 181/181 Host Tests + OpenOrbis Native Build PASS في Run 42608511066. No Download/Install/Provider activation.

### إصلاح 2026-11-09 — Search identity وMetadata diagnostics

- **Commit code:** `abb6bfc43aad78427741b6cbdce9c01c03b35ae2` ([CI](https://github.com/obsp081-coder/ps4-updates/actions/runs/42605227571)). إذا اختفت نتيجة Search مركّزة أثناء Rescan، يعود التركيز للـKeyboard بدل الانتقال الصامت إلى CUSA مختلفة؛ يحتفظ بالـQuery، ويُبقي التركيز عند إعادة الترتيب ما دامت الهوية نفسها موجودة. اختبارات Host في `tests/ui_mocks/navigation_smoke.c` تغطي الحالتين.
- **Diagnostic fix:** `Metadata records` يطبع `metadata_found` (جميع Base/Patch/DLC Metadata)، وليس `found` (Base فقط). اختبار 2 Base + 1 DLC يؤكد `Metadata records: 3` و`Base-game records: 2`.
- **تصحيح تفسير Host screenshot:** ظهور سجل gp في Home Preview القديم جاء من `ui_smoke.cpp` fixture مصطنع. مسار `ftp_scanner_v05.c` الإنتاجي يمرر `games[]` الخاصة بالـBase فقط إلى `scanner_ui_results`؛ سجلات gp/ac محفوظة بصورة مستقلة في `metadata_records` داخل Inventory. لا يوجد دليل على ظهور Patch ضمن مكتبة الألعاب الحقيقية.
- **الحالة:** Host 181/181 PASS + OpenOrbis Native Build PASS؛ لا يوجد اختبار على PS4 Firmware 14.00 لهذا التعديل. Download/Install غير مفعّلين.

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
