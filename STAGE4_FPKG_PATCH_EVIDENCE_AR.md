# المرحلة الرابعة — FPKG Patch Evidence Gate (Offline، Read Only)
**التاريخ:** 10 نوفمبر 2026
**Verdict:** MODIFY — ACCEPT لبوابة فحص بيانات الحزمة والسلامة بنهج Fail Closed؛ **DEFER** لأي إثبات FPKG/حقوق/توافق تثبيت.
**المنصة المستهدفة:** PS4 FW14.00 + GoldHEN. التنفيذ الحالي **Host Python فقط**، ولا يُستدعى من PS4 Runtime.
**الحد الأمني:** ليس Package Downloader ولا Installer. لم نختبر حزمة PS4 Patch حقيقية على الجهاز.

## دليل صيغة Metadata

- PS4 Developer Wiki PARAM.SFO يحدد `CATEGORY=gp` باعتبارها **Game Application Patch**، و`gd` باعتبارها **Game Digital Application**، و`ac` باعتبارها **Additional Content**: https://www.psdevwiki.com/ps4/PARAM.SFO
- OpenOrbis Building Homebrew يضع `gd` للـHomebrew base PKG ويوصي بمطابقة Content ID بين SFO والحزمة: https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/blob/master/docs/MD/Building%20Homebrew.md
- هذه المعاني هي **تصنيفات معلنة في param.sfo**؛ لا تثبت توقيع الحزمة أو كونها FPKG، ولا تطابقها مع FPKG Base معين. `gdp` غير مثبت باعتباره Game Application Patch في هذه المراجع، لذلك توقفنا عن اعتباره Update في `pkg_local_inspector.py` (Fail Closed).
- هذا الاختبار يرفض الفرق بين Base Content ID وPatch Content ID. الواقع قد يسمح به في حالات معينة، لكن يحتاج Binding مستقل مثبت على Hardware؛ مطابقة CUSA وحدها غير كافية.

## التنفيذ المنجز

1. `ps4-auto-updater-v0.3/pkg_local_inspector.py`: فصل `read_pkg_metadata_stream(stream)` عن Path Wrapper ليتمكن المدقق من قراءة Header وSFO على **نفس File Descriptor** المستخدم في SHA-256، دون Path Lookup جديد.
2. `ps4-auto-updater-v0.3/fpkg_patch_evidence.py`: مراجعة محلية لملف يملكه المستخدم، Manifest منفصل، Inventory Snapshot، وFirmware. لا HTTP أو Runtime API أو تعديل للعبة/حزمة. يفتح الملف `O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK` حيث تتوفر، ويرفض Nonregular files، ويقرأ SHA-256 بتدفق 1 MiB دون تحميل الملف في RAM، ويتحقق من `fstat` قبل وبعد ومن `lstat` على المسار للتحذير عند تغيّر الهوية أثناء الفحص.
3. Manifest مستقل بنمط `UNVERIFIED_LOCAL_MANIFEST`: `CUSA`, `Content ID`, `Patch Version`, `Required Base Version`, `Minimum FW`, `Exact Size`, `SHA-256`. **Manifest نفسه غير موثوق**: لا مفتاح Publisher Signature ولا إثبات الحقوق في هذا الطور. أي حقول إضافية (مثل `download_allowed`, `rights_verified`, `install_command`) مرفوضة.
4. يقرأ `PKG magic` وSFO/PKG IDs و`CATEGORY=gp` و`APP_VER` و`SYSTEM_VER` ثم يطابقها بالـManifest ويطابق SHA-256 وحجم الملف، ثم يقارن Content ID/Base Version/FW مع Inventory.
5. حتى إذا تطابق كل شيء، يعيد **`METADATA_HASH_MATCH_REVIEW_ONLY`** ولا يرفع `fpkg_confirmed` أو `publisher_rights_verified` أو `base_patch_compatibility_confirmed` أو `installed_confirmed` إلى True. يظل `download_allowed=false` و`install_allowed=false` دائمًا.
6. `tests/test_fpkg_patch_evidence.py`: ينتج Synthetic PKG مصغرًا للاختبار فقط، وليس PKG قابلًا للتثبيت؛ يختبر تطابق التجزئة، تغيير ملف بنفس الحجم، Symlinks، Directory، اختلاف CID/Version/FW، Duplicate Inventory، Source Flags مزورة، تغيير الملف أو استبدال مساره أثناء قراءة الـSFO، ومطابقة `gp` فقط.
7. هذه البوابة لا تُستخدم لاستنساخ ألعاب أو فك تشفير أو تجاوز تراخيص.

## طريقة تجربة Offline لملف مرخّص لدى المستخدم

يجب كتابة Manifest من **مصدر مستقل ومراجَع** يحدد حجم الحزمة وSHA-256 ومعلومات الإصدار. لا يعتبر مصدره معتمدًا تلقائيًا. مثال توضيحي للحقول (ليس قيمًا قابلة للتثبيت):

```json
{
  "schema": 1,
  "review_mode": "UNVERIFIED_LOCAL_MANIFEST",
  "declared_package_kind": "fpkg_patch",
  "cusa": "CUSA99999",
  "content_id": "EP9000-CUSA99999_00-TESTONLY00000000",
  "patch_version": "01.02",
  "required_base_version": "01.00",
  "minimum_fw": "11.02",
  "size_bytes": 9999,
  "sha256": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
}
```

تُستبدل هذه القيم الخيالية ببيانات مستقلة مطابقة للحزمة التي يملكها المستخدم. مثال CLI على الكمبيوتر (المدخلات يجب أن تكون موجودة بالفعل):

```sh
python3 ps4-auto-updater-v0.3/fpkg_patch_evidence.py \
  /safe/path/owned-patch.pkg \
  /safe/path/unverified-manifest.json \
  /safe/path/read-only-inventory.json \
  --firmware 14.00 --out /safe/path/review-only-report.json
```

البرنامج لا ينشئ PKG ولا يكتب فوق أي حزمة. يكتب التقرير فقط إذا طلب `--out`، **ولا يمنح صلاحيات**.

## نواقص لا يمكن تجاوزها بالـHost

- لا يصح اعتبار Hash لمجرد توافقه مع Manifest غير موقع دليلاً على Publisher أو حقوق توزيع. نحتاج Authoritative Provenance وربط إصدار الناشر الذي يملك الحق بهذه البصمة، ثم التفريق بين Full/Base Package وUpdate Patch حقيقية.
- بيانات `param.sfo` قابلة للتوليد في Fixture؛ Category=gp وSHA-256 لا يثبتان FPKG signature flags أو صلاحية AppInstUtil/BGFT.
- `O_NOFOLLOW` والتحقق من `fstat` يقللان خطر تغيير مسار الملف لكن لا يعطيان Snapshot ذرية ضد منافس يستطيع تعديل الملف نفسه أثناء الفحص أو بعده. لا يجوز تمرير Path فُحص مسبقًا إلى Installer لاحقًا دون Binding وتحقق جديدين.
- Inventory المصنَّف `package_kind=fpkg` ليس دليلًا مستقلًا أن اللعبة FPKG: يحتاج Scanner/hardware metadata موثق ومالك الناشر والدليل.
- لا يتعرف هذا المدقق على Installer FW14 ولا يختبر GoldHEN أو Storage Native. التنزيل/التثبيت في التطبيق مغلقان.

## البوابة التالية الموصى بها

1. الحصول على **Patch PKG مخوّل** من ناشر Homebrew يستطيع إثبات الحقوق، أو بناء Fixture Patch ملك المشروع إن ثبت مسار إنتاجها على أدوات PS4.
2. مراجعة Header/SFO وحجم/Digest على ملف مخوّل حقيقي، مع مقارنة نسخة Base مثبتة أو مملوكة للمطور؛ التحقق من أن `gp` Patch وليس `gd` Base.
3. تصميم Native Read-Only Proof على FW14 لتمييز FPKG base/update مع 로그 واضح، بلا تنزيل/تثبيت.
4. بعدها فقط فصل مصدر Download مصرح به، ثم TLS/Storage/Installer Hardware Gates كل على حدة.

**الأولوية:** لا ننشر Package Manager يدّعي العثور على تحديثات FPKG قابلة للتثبيت اعتمادًا على بيانات Metadata أو Hash غير موثوق.
