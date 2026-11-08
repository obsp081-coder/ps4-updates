"""Build-time Arabic labels rasterized from an OFL licensed Noto Sans Arabic.

Output is compact 2-bit-alpha bitmap data for PS4 framebuffer rendering.
No .ttf/.otf file is bundled with or read by the PS4 application.
This does not solve shaping of arbitrary dynamic game titles yet.
"""
import argparse
from pathlib import Path

LABELS = (
    ("HOME", "الرئيسية"),
    ("DETAILS", "تفاصيل اللعبة"),
    ("DOWNLOADS", "التنزيلات"),
    ("LIBRARY", "مكتبة الألعاب"),
    ("BASE_GAMES", "الألعاب الأساسية"),
    ("CUSA_FOLDERS", "مجلدات الألعاب"),
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
    ("SEARCH_BY_NAME", "البحث بالاسم أو CUSA"),
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
    ("BASE_UNKNOWN", "لعبة — التثبيت غير مؤكد"),
    ("GAME_INSTALLED", "اللعبة مثبتة"),
    ("GAME_NOT_INSTALLED", "اللعبة غير مثبتة"),
    ("NO_SELECTED_GAME", "لم تحدد لعبة"),
    ("NO_MATCHING_GAME", "لا توجد ألعاب مطابقة"),
    ("READING_METADATA", "جار فحص بيانات الألعاب"),
    ("NO_GAME_METADATA", "لا توجد بيانات ألعاب"),
    ("MISSING_INVENTORY", "تعذر حفظ قائمة الألعاب"),
    ("NO_FIRMWARE_DATA", "متطلبات Firmware غير متوفرة"),
    ("RETAIL_FPKG_WARNING", "تحديثات Retail لا تطابق FPKG تلقائيًا"),
    ("FOOTER_HOME", "الأسهم: اختيار    إكس: تفاصيل    مربع: التنزيلات    دائرة: خروج"),
    ("FOOTER_DETAILS", "دائرة: رجوع    التحديثات غير المؤكدة محظورة"),
    ("FOOTER_DOWNLOADS", "دائرة: رجوع    لا توجد تنزيلات معتمدة"),
    ("FOOTER_SCANNING", "جار قراءة بيانات الألعاب فقط"),
    ("ITEMS", "العناصر"),
    ("COVER_FALLBACK", "بدون صورة"),
    ("NO_ACTIVE_DOWNLOAD", "لا توجد تنزيلات نشطة"),
    ("NO_QUEUED_DOWNLOAD", "لا توجد مهام في الانتظار"),
    ("NO_INSTALL_JOB", "لا توجد عمليات تثبيت"),
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
        bbox = font.getbbox(caption, direction="rtl", language="ar")
        width = bbox[2] - bbox[0] + 4
        height = bbox[3] - bbox[1] + 4
        if not (1 <= width <= 900 and 1 <= height <= 90):
            raise ValueError(f"Illegal label dimensions: {name} {width}x{height}")
        image = Image.new("L", (width, height), 0)
        drawer = ImageDraw.Draw(image)
        drawer.text((2-bbox[0], 2-bbox[1]), caption, font=font,
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
