# قبول واجهة Royal Blue — المراجع المعتمدة وموانع الإغلاق

**التاريخ:** 9 نوفمبر 2026. **حالة القرار: MODIFY** — بنينا نظام رسم جديدًا؛ لم نحقق مطابقة 1:1، ولم نختبره على PS4 Firmware 14.00 حقيقي.

## المراجع التي وافق عليها المستخدم
1. `صورة ChatGPT 8 نوفمبر 2026، 06_40_38 م-1.png`: **Home**.
2. `صورة ChatGPT 8 نوفمبر 2026، 06_40_40 م-2.png`: **Details**.
3. `صورة ChatGPT 8 نوفمبر 2026، 06_40_44 م-3.png`: **Downloads**.

هذه أسماء مراجع في المحادثة وليست موارد مُوزَّعة أو مرخصة تلقائيًا للمشروع. جميع الصور المرجعية 1672×941؛ المقارنة البصرية تكون بعد تصغير/تكبير نسبيًا إلى لقطة Host الفعلية 1920×1080، دون الادعاء بتطابق البكسلات بسبب اختلاف بيانات الألعاب.

## أولويات الفروق بحسب الأثر البصري

| العنصر | المرجع | التطبيق الحالي | القرار |
|---|---|---|---|
| خلفية 16:9 سينمائية | Artwork للعبة المحددة، موزع كخلفية ضبابية كاملة | `icon0.png` مربع محلي، أو Navy Gradient؛ Cloud/Blur منسوخ من الأيقونة وليس صورة 16:9 | **BLOCKER بصري رئيسي.** إثبات قراءة Artwork عريض مرخّص من مصدر اللعبة محليًا قبل تنفيذ واجهة ادعاءها سينمائي |
| Sidebar | نحو 300px عند التحجيم إلى 1920px، أقسام مضغوطة عموديًا | 252px؛ أصبحت المسافة العمودية 77px بدل 92px | يلزم توسيع وتحريك Content دون إخفاء الجدول |
| Header | Controller Icon جميل، عنوان أبيض Bold، أقواس Light Sweep | Controller هندسي بسيط، Header Static، Title Glyphs | إجراء Visual Polish للأيقونة الأصلية بدون نسخ تصميم مسجل |
| البطاقات | Translucent Glass / Glow، حواف ناعمة | أضيف `ui_visual_fx.h`، وGradient/Glass ثابت CPU-only | فحص Host Perf ثم FW14 VideoOut الحقيقي |
| Home | 6 صفوف، Covers على اليمين، Game Data + Status حقيقي | 6 Slots لكن تُرسم سجلات الألعاب الموجودة فقط، بلا إدخالات وهمية | يمكن رسم Empty Rows شفافة لكن لا تُدرج Fake Games أو CUSA |
| Details | بطاقات 6، Compatible Verdict، Versions Table، CTA | البنية موجودة وحالة UNKNOWN عند نقص دليل | لا تستخدم Green Compatible أو تفعّل Download/Install دون مصدر موثوق |
| Downloads | حالات وتنزيلات تقدم حقيقي | بنية UI موجودة لكن لا يوجد Provider فعّال؛ تعرض Empty State | لا اختراع ETA/Speed/Progress |
| Footer | Gamepad Symbols وأشرطة واضحة، نحو 120px ارتفاعًا بعد Scale | Footer عند y=1000 بارتفاع 80px ونص إرشادي | إعادة توازن بعد تثبيت أبعاد محتوى Home |

الأبعاد تقريبية مستخرجة بصريًا من الصور، **ليست** Pixel-Perfect Design Tokens مُعتمدة بعد.

## نتائج تقنية قيد التحقق
- **Verified (code/Host only):** `ui_visual_fx.h` يتيح Native 1080p Glass/Gradient/Halo بدون Shader أو صور خارجية أو Heap، ويعمل فقط وقت تغيّر الحالة. `BLUR_WIDTH=240`, `BLUR_HEIGHT=135` تبقي صورة الخلفية المحوّلة من `icon0` ذات تفاصيل أنعم، وإن ظلت المصدر نفسه.
- **Partially verified (external metadata references):** PS4 PKG metadata قد يحتوي `pic1.png` و`pic0.png` و`icon0.png`. المراجع: https://www.psdevwiki.com/ps4/PKG_files و https://github.com/shadps4-emu/shadPS4/wiki/PKG-Information.
- **Unverified on GoldHEN FW14:** إمكانية `RETR /user/appmeta/<CUSA>/pic1.png`، أو أي مسار FTP آخر، ومدى توافر الملف حسب كل لعبة، وقيود حجم PNG ودعم النقل. لا تفعّل المسار قبل اختبار FTP Read-Only واستراتيجية Memory Cap وPNG CRC وتفريغ Worker الآمن. لا تُخمن مسارات على أجهزة المستخدم.
- **Existing comparable architecture:** Itemzflow يسمح بتغيير خلفية البرنامج إلى PNG محلية في إعداداته؛ هذا إثبات لتصميم Theme Asset عام، **لا** يثبت توفر Artwork لكل لعبة عندنا. المصدر: https://github.com/LightningMods/Itemzflow.
- **Needs real-hardware testing:** GoldHEN FW14، استهلاك RAM/CPU مع الخلفية الجديدة، إطلاق التطبيق والخروج، FTP المحلي، فشل النقل وإغلاق Worker.

## معايير الإغلاق البصرية (لا يفي بها GitHub CI وحده)
1. مقارنات `Home` و`Details` و`Downloads` من **نفس Commit** مقابل صور المستخدم، والأهم عرض صورة لعبة 16:9 محلية حقيقية حين تتوافر.
2. العربية والإنجليزية والمزج، بأسماء طويلة/قصيرة، دون قصّ أو تداخل.
3. حالات 0/1/6/أكثر ألعاب، Cached/Stale/Scan Failure، دون أي تحديث أو تنزيل وهمي.
4. R1/L1 يمرّ على 8 Tabs فعليًا مع دورة كاملة؛ **Settings + R1 -> Home**؛ Search وDetails شاشتان فرعيتان كما هو محدد.
5. التحقق من الأداء والخروج وتحديث الأيقونات على PS4 حقيقي، وليس Host أو shadPS4 فقط.
6. عدم رفع Final PKG أو إعلان اكتمال UI قبل تحقق المستخدم من نسخة Test جديدة على الأقل.

**قاعدة:** لا تُكرّر وصف التصميم بأنه «مطابق للصور» قبل إثبات ذلك. الصور المولّدة لأغراض Concept ليست اختبارًا للتطبيق الحقيقي، والـHost Screenshot ليست لقطة من PS4 Hardware.
