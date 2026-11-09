"""Build-time Arabic labels rasterized from an OFL licensed Noto Sans Arabic.

Output is compact 2-bit-alpha bitmap data for PS4 framebuffer rendering.
No .ttf/.otf file is bundled with or read by the PS4 application.
This does not solve shaping of arbitrary dynamic game titles yet.
"""
import argparse
from pathlib import Path

LABELS = (
    ("HOME", "الرئيسية"),
    ("GAME_NAME_UNAVAILABLE", "اسم اللعبة غير متوفر"),
    ("APP_NAME", "مدير تحديثات ألعاب بلايستيشن"),
    ("LOCAL_READ_ONLY", "المكتبة المحلية للعرض فقط"),
    ("JOB_DOWNLOADING", "جار التنزيل"),
    ("JOB_QUEUED", "في الانتظار"),
    ("JOB_PAUSED", "متوقف مؤقتا"),
    ("JOB_COMPLETED", "اكتمل التنزيل"),
    ("FINISHED_DOWNLOAD", "تنزيلات مكتملة"),
    ("ACTIVE_DOWNLOADS", "التنزيلات النشطة"),
    ("QUEUED_DOWNLOADS", "مهام في الانتظار"),
    ("FOOTER_JOBS", "الأسهم: تصفح    دائرة: رجوع    حالات للعرض فقط"),
    ("JOB_FAILED", "فشل التنزيل"),
    ("JOB_BLOCKED", "التنزيل محظور"),
    ("UPDATE_REVIEW", "تحديث للعرض فقط"),
    ("UPDATE_BLOCKED", "التحديث محظور"),
    ("DETAILS", "تفاصيل اللعبة"),
    ("RAIL_DETAILS", "تفاصيل اللعبة"),
    ("DOWNLOADS", "التنزيلات"),
    ("SETTINGS", "الإعدادات"),
    ("SETTINGS_HEADER", "إعدادات البرنامج"),
    ("LANGUAGE_SETTING", "اللغة"),
    ("ARABIC_SETTING", "العربية"),
    ("THEME_SETTING", "المظهر"),
    ("ROYAL_BLUE_SETTING", "الأزرق الملكي"),
    ("STORAGE_SETTING", "تخزين التحديثات"),
    ("INTERNAL_SETTING", "غير مفعّل حاليًا"),
    ("SOURCE_SETTING", "مصادر التحديثات"),
    ("TRUSTED_SETTING", "لم يربط مصدر تحديثات بعد"),
    ("INSTALL_SETTING", "التثبيت"),
    ("INSTALL_OFF_SETTING", "معطل حتى التحقق من التوافق"),
    ("SETTINGS_NOTE", "هذه خيارات العرض المعتمدة، ولا تغير ملفات الألعاب"),
    ("FOOTER_SETTINGS", "دائرة: رجوع    الخيارات الحالية للعرض فقط"),
    ("LIBRARY", "مكتبة الألعاب"),
    ("GAME_COLUMN", "اللعبة"),
    ("STATE_COLUMN", "الحالة"),
    ("BASE_GAMES", "الألعاب الأساسية"),
    ("INSTALL_UNKNOWN", "تثبيت غير مؤكد"),
    ("SCAN_ERRORS", "أخطاء الفحص"),
    ("SELECTED_GAME", "اللعبة المحددة"),
    ("VERSION", "الإصدار"),
    ("CATEGORY", "نوع البيانات"),
    ("UNKNOWN", "غير مؤكد"),
    ("UNAVAILABLE", "غير متوفر"),
    ("NO_TRUSTED_UPDATE", "لا يوجد تحديث موثوق متاح"),
    ("DOWNLOAD_BLOCKED", "التنزيل والتثبيت غير متاحين"),
    ("SCAN_INCOMPLETE", "الفحص غير مكتمل"),
    ("SCAN_COMPLETE", "انتهى الفحص"),
    ("SFO_METADATA", "بيانات اللعبة"),
    ("FULL_DETAILS", "التفاصيل الكاملة"),
    ("GAME_SIZE", "حجم اللعبة"),
    ("REGION", "المنطقة"),
    ("FIRMWARE", "متطلبات النظام"),
    ("AVAILABLE_UPDATES", "إصدارات التحديث المتاحة"),
    ("NO_DOWNLOAD_TASKS", "لا توجد تنزيلات معتمدة"),
    ("INSTALL_BLOCKED", "التثبيت محظور حتى التحقق"),
    ("DATA_NOT_PROOF", "وجود البيانات لا يؤكد تثبيت اللعبة"),
    ("SEARCH_BY_NAME", "البحث باسم اللعبة أو معرّفها"),
    ("WAITING", "قيد الانتظار"),
    ("SOURCE", "مصدر التحديث"),
    ("PACKAGE", "نوع الحزمة"),
    ("STORAGE", "التخزين الداخلي"),
    ("NO_COVER", "لا توجد صورة"),
    ("PAUSE_NOT_SUPPORTED", "الإيقاف غير متاح"),
    ("NO_NETWORK_DOWNLOADS", "لا يمكن التنزيل قبل التحقق"),
    ("NO_INSTALL_API", "التثبيت غير متاح حاليًا"),
    ("PATCH_METADATA", "بيانات تحديث"),
    ("DLC_METADATA", "بيانات إضافة"),
    ("OTHER_METADATA", "بيانات أخرى"),
    ("UNVERIFIED_METADATA", "بيانات غير مؤكدة"),
    ("BASE_UNKNOWN", "لعبة: التثبيت غير مؤكد"),
    ("GAME_INSTALLED", "اللعبة مثبتة"),
    ("GAME_NOT_INSTALLED", "اللعبة غير مثبتة"),
    ("NO_SELECTED_GAME", "لم تحدد لعبة"),
    ("NO_MATCHING_GAME", "لا توجد ألعاب مطابقة"),
    ("READING_METADATA", "جار فحص بيانات الألعاب"),
    ("CACHE_PREVIEW", "بيانات محفوظة من فحص سابق: جاري التحقق"),
    ("CACHE_ONLY", "قد تكون الألعاب تغيرت منذ الفحص السابق"),
    ("CACHE_STALE", "بيانات محفوظة غير مؤكدة: الفحص غير مكتمل"),
    ("NO_GAME_METADATA", "لا توجد بيانات ألعاب"),
    ("EMPTY_LIBRARY_HELP", "تحقق من خادم جولدهن عند الحاجة"),
    ("MISSING_INVENTORY", "تعذر حفظ قائمة الألعاب"),
    ("NO_FIRMWARE_DATA", "متطلبات النظام غير متوفرة"),
    ("RETAIL_FPKG_WARNING", "تحديثات النسخ الأصلية لا تطابق النسخ المعدّلة تلقائيًا"),
    ("FOOTER_HOME", "الأسهم: اختيار    إكس: تفاصيل    مربع: التنزيلات    دائرة: خروج"),
    ("FOOTER_DETAILS", "دائرة: رجوع    التحديثات غير المؤكدة محظورة"),
    ("FOOTER_DOWNLOADS", "دائرة: رجوع    لا توجد تنزيلات معتمدة"),
    ("FOOTER_SCANNING", "جار قراءة بيانات الألعاب فقط"),
    ("ITEMS", "العناصر"),
    ("COVER_FALLBACK", "بدون صورة"),
    ("NO_ACTIVE_DOWNLOAD", "لا توجد تنزيلات نشطة"),
    ("NO_QUEUED_DOWNLOAD", "لا توجد مهام في الانتظار"),
    ("NO_INSTALL_JOB", "لا توجد عمليات تثبيت"),
    ("SEARCH_RESULTS", "نتائج البحث"),
    ("SEARCH_MENU", "البحث"),
    ("SEARCH_HINT", "ابحث باسم اللعبة أو معرّفها"),
    ("KEYBOARD_ARABIC", "لوحة مفاتيح عربية"),
    ("KEYBOARD_ENGLISH", "لوحة مفاتيح إنجليزية"),
    ("FOOTER_SEARCH", "الأسهم: حروف ونتائج    إكس: كتابة أو تفاصيل    مربع: حذف    مثلث: لغة    دائرة: رجوع"),
    ("NEXT_MENU", "التالي"),
    ("PREV_MENU", "السابق"),
    ("WELCOME_TITLE", "مرحباً بك في مدير تحديثات ألعاب بلايستيشن"),
    ("WELCOME_NOTE", "افحص مكتبتك المحلية واستعرض المعلومات المتاحة بأمان"),
    ("GAMES_MENU", "الألعاب"),
    ("AVAILABLE_MENU", "التحديثات المتاحة"),
    ("QUEUE_MENU", "قائمة الانتظار"),
    ("INSTALLED_MENU", "التحديثات المثبتة"),
    ("ABOUT_MENU", "حول البرنامج"),
    ("QUEUE_EMPTY", "لا توجد مهام انتظار فعلية"),
    ("INSTALLED_UNKNOWN", "لم نتحقق من التحديثات المثبتة بعد"),
    ("UPDATES_UNVERIFIED", "لا يمكن اعتبار بيانات التحديث تحديثاً موثوقاً"),
    ("ABOUT_NOTE", "نسخة تجريبية للقراءة فقط دون تنزيل أو تثبيت"),
    ("ABOUT_SAFETY", "البيانات المحلية لا تؤكد تثبيت الألعاب"),
    ("METRIC_GAMES", "إجمالي الألعاب"),
    ("METRIC_UNKNOWN", "تثبيت غير مؤكد"),
    ("METRIC_SCAN", "أخطاء الفحص"),
    ("METRIC_ACTIVE", "قيد التنزيل"),
    ("COMPATIBILITY_HEADING", "حالة التوافق"),
    ("COMPATIBILITY_UNKNOWN", "التوافق غير مؤكد"),
    ("ACTION_DOWNLOAD_DISABLED", "التنزيل معطّل"),
    ("ACTION_QUEUE_DISABLED", "الانتظار معطّل"),
    ("ACTION_INSTALL_DISABLED", "التثبيت معطّل"),
    ("DOWNLOAD_LIST_TITLE", "قائمة التنزيل"),
    ("SELECTED_JOB", "تفاصيل المهمة المحددة"),
    ("NO_SELECTED_JOB", "لا توجد مهمة محددة"),
    ("DOWNLOAD_PREVIEW", "التنزيل متوقف حتى التحقق من المصادر والتوافق"),
    ("TABLE_META_VERSION", "إصدار البيانات"),
    ("TABLE_UPDATE_VERSION", "التحديث المرصود"),
    ("TABLE_UPDATE_SIZE", "الحجم"),
    ("TABLE_STATE_UNKNOWN", "غير مؤكد"),
    ("TABLE_STATE_BASE", "بيانات لعبة"),
    ("TABLE_STATE_PATCH", "بيانات تحديث"),
    ("TABLE_STATE_DLC", "بيانات إضافة"),
    ("TABLE_STATE_OTHER", "بيانات أخرى"),
)


def generate(output: Path, font_path: Path, font_size: int = 31):
    from PIL import Image, ImageDraw, ImageFont, features
    if not features.check("raqm"):
        raise RuntimeError("Pillow must have libraqm for proper Arabic shaping")
    if not font_path.is_file():
        raise FileNotFoundError("Arabic font missing (install fonts-noto-core)")
    # Ubuntu 22.04's packaged Pillow predates ImageFont.Layout enum.
    # Both API generations expose the same libraqm shaping engine.
    layout = getattr(getattr(ImageFont, "Layout", None), "RAQM", None)
    if layout is None:
        layout = ImageFont.LAYOUT_RAQM
    font = ImageFont.truetype(str(font_path), font_size,
                              layout_engine=layout)
    # Heading glyphs ship as bitmaps only. No font file or shape engine
    # is needed on the console itself. Weight is part of the design tokens.
    bold_path = font_path.parent / "NotoSansArabic-Bold.ttf"
    semi_path = font_path.parent / "NotoSansArabic-SemiBold.ttf"
    if not bold_path.is_file() or not semi_path.is_file():
        raise FileNotFoundError(
            "Noto Sans Arabic Bold/SemiBold missing (fonts-noto-core)"
        )
    bold_font = ImageFont.truetype(
        str(bold_path), 31, layout_engine=layout
    )
    bold_title_font = ImageFont.truetype(
        str(bold_path), 33, layout_engine=layout
    )
    semibold_rail_font = ImageFont.truetype(
        str(semi_path), 24, layout_engine=layout
    )
    # The 252px Sidebar has only 154px between its icon and right gutter.
    # Rasterize the long caption once at build time, not during frames.
    rail_font = ImageFont.truetype(str(font_path), 26,
                                   layout_engine=layout)
    rail_long_font = ImageFont.truetype(str(font_path), 20,
                                         layout_engine=layout)
    metric_font = ImageFont.truetype(str(font_path), 22,
                                     layout_engine=layout)
    table_font = ImageFont.truetype(str(font_path), 21,
                                    layout_engine=layout)
    header_font = ImageFont.truetype(str(font_path), 25,
                                     layout_engine=layout)
    header_note_font = ImageFont.truetype(str(font_path), 19,
                                          layout_engine=layout)
    welcome_font = ImageFont.truetype(str(font_path), 29,
                                      layout_engine=layout)
    welcome_note_font = ImageFont.truetype(str(font_path), 21,
                                           layout_engine=layout)
    header = [
        "/* GENERATED at build time: Arabic shaped RTL label masks, 2 bits/px.",
        " * Source NotoSansArabic-Regular (SIL Open Font License 1.1).",
        " * Do not edit manually. No font file is embedded. */",
        "#ifndef PS4_UI_ARABIC_GENERATED_H",
        "#define PS4_UI_ARABIC_GENERATED_H",
        "#include <stdint.h>",
        "typedef struct UiArabicGlyph {",
        "  uint32_t offset; uint16_t width; uint16_t height;",
        "} UiArabicGlyph;",
        "enum UiArabicLabel {",
    ]
    for i, (name, _) in enumerate(LABELS):
        header.append(f" UI_AR_{name} = {i},")
    header.extend([
        f" UI_AR_COUNT = {len(LABELS)}",
        "};",
    ])
    atlas = bytearray()
    entries = []
    for name, caption in LABELS:
        label_font = (
            bold_title_font if name in (
                "WELCOME_TITLE", "DOWNLOAD_LIST_TITLE",
                "AVAILABLE_UPDATES", "SELECTED_GAME", "SELECTED_JOB"
            ) else
            bold_font if name in (
                "LIBRARY", "COMPATIBILITY_HEADING", "SETTINGS_HEADER",
                "DETAILS", "NO_TRUSTED_UPDATE"
            ) else
            semibold_rail_font if name in (
                "HOME", "GAMES_MENU", "QUEUE_MENU", "ABOUT_MENU",
                "DOWNLOADS", "SETTINGS"
            ) else
            header_font if name == "APP_NAME" else
            header_note_font if name == "LOCAL_READ_ONLY" else
            table_font if name.startswith("TABLE_") else
            metric_font if name.startswith("METRIC_") else
            rail_long_font if name in ("AVAILABLE_MENU", "INSTALLED_MENU")
            else rail_font if name in (
                "RAIL_DETAILS", "GAMES_MENU", "QUEUE_MENU", "ABOUT_MENU",
                "DOWNLOADS", "SETTINGS", "HOME")
            else welcome_font if name == "WELCOME_TITLE"
            else welcome_note_font if name == "WELCOME_NOTE"
            else font
        )
        bbox = label_font.getbbox(caption, direction="rtl", language="ar")
        width = bbox[2] - bbox[0] + 4
        height = bbox[3] - bbox[1] + 4
        if not (1 <= width <= 1100 and 1 <= height <= 90):
            raise ValueError(f"Illegal label dimensions: {name} {width}x{height}")
        image = Image.new("L", (width, height), 0)
        drawer = ImageDraw.Draw(image)
        drawer.text((2-bbox[0], 2-bbox[1]), caption, font=label_font,
                    direction="rtl", language="ar", fill=255)
        mask = list(image.getdata())
        packed = bytearray((len(mask) + 3)//4)
        for i, alpha in enumerate(mask):
            level = min(3, (alpha + 42)//85)
            packed[i//4] |= level << ((3 - i % 4)*2)
        entries.append((len(atlas), width, height))
        atlas.extend(packed)
    header.append("static const UiArabicGlyph UI_AR_GLYPHS[UI_AR_COUNT] = {")
    header.extend(f"  {{{offset}u,{width}u,{height}u}},"
                  for offset,width,height in entries)
    header.append("};")
    header.append(f"static const uint8_t UI_AR_PIXELS[{len(atlas)}] = {{")
    for i in range(0, len(atlas), 24):
        header.append("  " + ",".join(f"0x{x:02x}" for x in atlas[i:i+24]) + ",")
    header.append("};")

    # Build a small presentation-form atlas for dynamic Arabic game titles.
    # This is intentionally conservative: basic Arabic joining, not a claim
    # to implement full Unicode Bidirectional Algorithm or every script.
    import unicodedata
    forms = ("isolated", "final", "initial", "medial")
    decompositions = {}
    for codepoint in [*range(0xFB50, 0xFE00), *range(0xFE70, 0xFF00)]:
        parts = unicodedata.decomposition(chr(codepoint)).split()
        if (len(parts) == 2 and parts[0].startswith("<") and
                parts[0][1:-1] in forms):
            base = int(parts[1], 16)
            if 0x0621 <= base <= 0x08FF:
                decompositions.setdefault(base, {})[parts[0][1:-1]] = codepoint
    cps = sorted(set(c for entry in decompositions.values() for c in entry.values()))
    if not 50 <= len(cps) <= 600:
        raise ValueError(f"Unexpected Arabic presentation forms: {len(cps)}")
    codepoint_to_index = {cp: index for index, cp in enumerate(cps)}
    dynamic_atlas = bytearray()
    dynamic_entries = []
    for cp in cps:
        char = chr(cp)
        # The glyph canvas has a 3px transparent safety margin on each
        # side. The pen advance must NOT include those margins, otherwise
        # connected Arabic presentation forms are visibly separated.
        advance = max(2, int(round(font.getlength(char, direction="ltr"))))
        width = max(8, advance + 6)
        height = font_size * 2 + 8
        if width > 110:
            raise ValueError(f"Suspicious Arabic glyph {cp:04X} width {width}")
        image = Image.new("L", (width, height), 0)
        drawer = ImageDraw.Draw(image)
        drawer.text((3, font_size + 11), char, font=font,
                    anchor="ls", direction="ltr", fill=255)
        raw = list(image.getdata())
        packed = bytearray((len(raw) + 3)//4)
        for k, a in enumerate(raw):
            packed[k//4] |= min(3, (a + 42)//85) << (2*(3-k%4))
        dynamic_entries.append((len(dynamic_atlas), width, height, advance))
        dynamic_atlas.extend(packed)
    header.append("typedef struct UiArabicDynamicGlyph {")
    header.append(" uint32_t offset; uint16_t width; uint16_t height;")
    header.append(" uint16_t advance; /* excludes 3px canvas margins */")
    header.append("} UiArabicDynamicGlyph;")
    header.append(f"#define UI_AR_DYNAMIC_COUNT {len(cps)}u")
    header.append("static const UiArabicDynamicGlyph UI_AR_DYNAMIC_GLYPHS[] = {")
    for offset, width, height, advance in dynamic_entries:
        header.append(f" {{{offset}u,{width}u,{height}u,{advance}u}},")
    header.append("};")
    header.append(f"static const uint8_t UI_AR_DYNAMIC_BITS[{len(dynamic_atlas)}] = {{")
    for i in range(0, len(dynamic_atlas), 24):
        header.append(" " + ",".join(f"0x{x:02x}" for x in dynamic_atlas[i:i+24]) + ",")
    header.append("};")
    header.append("typedef struct UiArabicJoining {")
    header.append(" uint32_t base; uint16_t isolated, final, initial, medial;")
    header.append("} UiArabicJoining;")
    header.append(f"#define UI_AR_JOINING_COUNT {len(decompositions)}u")
    header.append("static const UiArabicJoining UI_AR_JOINING[] = {")
    for base, variants in sorted(decompositions.items()):
        indexes = [codepoint_to_index.get(variants.get(form), 65535)
                   for form in forms]
        header.append(" {0x%04Xu,%s}," % (
            base, ",".join("%du" % idx for idx in indexes)
        ))
    header.append("};")

    # Small OFL Noto Sans Latin glyph atlas for the same CPU framebuffer.
    # Improves the look of English game names/CUSA without font engines,
    # dynamic heap allocation, font files or antialias work on the PS4.
    latin_path = font_path.parent / "NotoSans-Regular.ttf"
    if not latin_path.is_file():
        raise FileNotFoundError(
            "Latin font missing (install fonts-noto-core)"
        )
    latin_bytes = bytearray()
    latin_rows = []
    for latin_size in (23, 32, 42):
        latin_font = ImageFont.truetype(str(latin_path), latin_size)
        glyphs = []
        for codepoint in range(32, 127):
            letter = chr(codepoint)
            bounds = latin_font.getbbox(letter)
            width = max(2, bounds[2] - bounds[0] + 4)
            height = max(2, bounds[3] - bounds[1] + 4)
            advance = max(2, int(round(latin_font.getlength(letter))) + 1)
            if not (width <= 80 and height <= 80 and advance <= 80):
                raise ValueError("Latin glyph size exceeds safe limit")
            canvas = Image.new("L", (width, height), 0)
            ImageDraw.Draw(canvas).text(
                (2 - bounds[0], 2 - bounds[1]),
                letter, font=latin_font, fill=255
            )
            pixels = list(canvas.getdata())
            packed = bytearray((len(pixels) + 3) // 4)
            for k, alpha in enumerate(pixels):
                packed[k//4] |= min(3, (alpha + 42)//85) << (2*(3-k%4))
            glyphs.append((len(latin_bytes), width, height, advance))
            latin_bytes.extend(packed)
        latin_rows.append(glyphs)
    header.append("/* OFL Noto Sans Latin, raster glyphs only; no font files. */")
    header.append("typedef struct UiLatinGlyph {")
    header.append(" uint32_t offset; uint16_t width,height,advance;")
    header.append("} UiLatinGlyph;")
    header.append("static const UiLatinGlyph UI_LATIN_GLYPHS[3][95] = {")
    for row in latin_rows:
        header.append(" {")
        for off,w,h,advance in row:
            header.append(f"  {{{off}u,{w}u,{h}u,{advance}u}},")
        header.append(" },")
    header.append("};")
    header.append(f"static const uint8_t UI_LATIN_BITS[{len(latin_bytes)}] = {{")
    for i in range(0, len(latin_bytes), 24):
        header.append(
            " " + ",".join(f"0x{x:02x}" for x in latin_bytes[i:i+24]) + ","
        )
    header.append("};")
    header.append("#endif")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(header)+"\n", encoding="ascii")
    return len(atlas)


def main():
    p=argparse.ArgumentParser()
    p.add_argument("--output", required=True, type=Path)
    p.add_argument("--font", type=Path, default=Path(
        "/usr/share/fonts/truetype/noto/NotoSansArabic-Regular.ttf"
    ))
    ns=p.parse_args()
    print("Generated Arabic UI atlas bytes:",generate(ns.output,ns.font))


if __name__=="__main__":
    main()
