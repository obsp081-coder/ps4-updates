# PS4 FW14 — اختبار PKG Header على جهاز حقيقي (Diagnostic Only)
**النسخة:** 2026-11-10
**حالة التفعيل:** مسار GitHub Actions يدوي، لا إصدار نهائي ولا إثبات اختبار جهاز.
**Verdict:** MODIFY / NEEDS REAL-HARDWARE TESTING

## لماذا نسخة مستقلة؟
- النسخة اليومية تستخدم `Title ID=BREW00197` وتكتب إلى `/data/PS4AutoUpdater`.
- حزمة الاختبار المنفصلة تستخدم `Title ID=BREW01998` و`Content ID=IV0000-BREW01998_00-FW14HEADERPROBE0`، وتحفظ التقارير والكاش في **`/data/PS4AutoUpdaterProbe`**.
- التأكد من عدم وجود تطبيق آخر يحمل `BREW01998` على جهازك ضروري قبل تثبيت هذه الحزمة. لا تفترض أن Title ID خالٍ عالميًا.
- الفحص في نسخة الاختبار يُقرأ إلى الثمانية بايت الأولى من `app.pkg` للألعاب التي حددها Scanner (المسارات المعتادة: `/user/app/<CUSA>/app.pkg` و`/mnt/ext0/user/app/<CUSA>/app.pkg` و`/mnt/ext1/user/app/<CUSA>/app.pkg`). هذا ليس فحصًا كاملًا لحزمة أو توقيع، بل مصدر معلومات عن Header فقط.
- لا يوجد تحديث عبر Sony، أو تنزيل PKG من الإنترنت، أو تثبيت باتشات، أو تعديل ألعاب/حفظ في هذا البناء.

## كيف تبني نسخة الاختبار؟
1. افتح [GitHub Actions / PS4 Native Scanner Build (Experimental)](https://github.com/obsp081-coder/ps4-updates/actions/workflows/ps4-build.yml).
2. اختر **Run workflow** وحدد الفرع `fix/scanner-cid-and-inventory-capacity`.
3. فعّل خيار `Build separate read-only BREW01998 header diagnostic PKG` (`fw14_header_probe_pkg`).
4. بعد نجاح الخطوات، افتح نفس Run وحمّل Artifact باسم يبدأ `fw14-readonly-header-probe-` (ZIP). سيبقى **7 أيام فقط**، وليس Github Release.
5. افتح `DIAGNOSTIC_MANIFEST.txt` وقارن `SHA256` مع ملف `.pkg` الموجود داخله، وتحقق أن Content ID هو `IV0000-BREW01998_00-FW14HEADERPROBE0`. **لا تثبّت** إن كانت البيانات مختلفة.
6. انسخ PKG إلى USB وثبّتها باستخدام مسار Homebrew/Package Installer الذي سبق التأكد من عمله على GoldHEN لديك. **لا تستخدم System Firmware Update**، ولا تُزل أو تستبدل أي تطبيق حالي.
7. شغّل `PS4 FPKG Header Probe - Read Only` وتحقق أن البرنامج يخرج إلى القائمة دون Crash. عند ظهور CE-34878-0 أو تجمد، توقف؛ لا تعيد محاولة تثبيت تحديث أو تغيير GoldHEN بسبب الفحص.

ملاحظة: إذا ظهر Run workflow لكن لم يظهر خيار `fw14_header_probe_pkg` بسبب اعتماد واجهة GitHub على Workflow الموجود في `main`، **لا تختَر Run بدون الخيار**: سيُبنى التطبيق العادي فقط، ولن يُرفع Artifact التشخيصي. بديل من GitHub CLI بعد تسجيل دخولك:

```sh
gh workflow run ps4-build.yml \
  --repo obsp081-coder/ps4-updates \
  --ref fix/scanner-cid-and-inventory-capacity \
  -f fw14_header_probe_pkg=true
```

ثم افتح التشغيل الناتج في GitHub Actions وحمّل Artifact بعد نجاحه. هذا يطلب **بناءًا جديدًا** وليس اختبارًا تلقائيًا على PS4. لا تدمج Draft PR أو تغيّر إصدار Main فقط لتفعيل خيار الاختبار.

## ما الذي ترسله من الجهاز؟
باستخدام خدمة GoldHEN FTP الموجودة عندك، **انسخ للقراءة فقط**:
- `/data/PS4AutoUpdaterProbe/diagnostic_v065.txt`
- `/data/PS4AutoUpdaterProbe/inventory_v065.json`

وأرسل نتيجتي الملفات بعد حذف أي أسماء خاصة/معلومات لا ترغب بنشرها. لا حاجة لإرسال PKG ألعاب أو ملفات Save Data.

## كيف نقيم نجاح الاختبار؟
- **أول دليل مطلوب:** يكتب الفحص سطر `PKG_SELF_HEADER BREW01998 observation=... location=... header_read=... flags=...` عن **PKG الخاصة بالتطبيق التشخيصي نفسه**، حتى لو لم تظهر أي ألعاب CUSA. `OPEN_FAILED` يعني عدم القدرة على القراءة أو عدم وجود المسار، ولا يعني Retail.
- يجب أن يثبت Log التشغيل `SCAN_STAGE_PKG_HEADER_PROBE_DONE: 1`. وقد تظهر أسطر `PKG_HEADER CUSAxxxxx observation=...` إذا وجد Scanner بيانات ألعاب CUSA، لكن **لا نحتاج أي ألعاب تجارية** لاختبار المسار الأول.
- `FAKE_FLAG_CANDIDATE` = تلميح من 8 بايت. **لا يعني تلقائيًا FPKG مؤكدة**. يمكن أن تظهر `OPEN_FAILED` بسبب صلاحيات أو اختلاف المسار. هذا فشل إثبات، لا دليل Retail.
- إذا لم يظهر أي CUSA بعد الفحص، افحص اتصال GoldHEN FTP وتقرير `scan_complete` دون تغيير ملفات ألعاب أو تثبيت باتشات.
- يقف الاختبار حتى بعد نجاح قراءة Header عند `fpkg_detection_confirmed:false`، و`download_enabled:false` و`install_enabled:false`.
- المطلوب التالي لتحسين موثوقية التصنيف: مقارنة النتيجة بحزمة Homebrew مملوكة لمطورها ومعروفة نوعها ثم دراسة قراءة حقول توقيع إضافية آمنة على FW14؛ Header وحده لا يضمن التوافق أو حقوق التوزيع.

## قواعد السلامة
- تثبيت PKG تجريبية بحد ذاته عملية نظام قابلة للفشل، حتى لو كان التطبيق بعد التشغيل Read-only؛ احتفظ بنسخة احتياطية من ملفاتك المهمة، ولا تختبر على حساب/جهاز أساسي دون قبول مخاطرة الاختبار.
- لا تنشر `PS4AutoUpdaterProbe` باسم أداة تحديث جاهزة.
- لا تدخل `--fw14_header_probe_pkg` في Build الافتراضي؛ الشرط يدوي والفرع Draft.
- لا نحذف `/data/PS4AutoUpdater` ولا نلمس ألعاب PS4 أو Save Data.

## حدود الأدلة حتى الآن
OpenOrbis Build يثبت القدرة على البناء، وHost Tests تثبت المسارات الاصطناعية والتحقق من حواجز الأمان. **بدون سجلات الجهاز الحقيقي لا نؤكد FW14 file permissions ولا صحة التصنيف.**
