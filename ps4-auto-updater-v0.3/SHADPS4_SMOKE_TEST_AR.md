# تشغيل نسخة PS4 Auto Updater التجريبية على shadPS4

**الغرض:** اختبار بدء التشغيل، Native VideoOut، التنقل باستخدام يد التحكم، الخروج والـLogs. **ليس** اختبار GoldHEN 14.00 أو تنزيل/تثبيت التحديثات.

## ما الذي ستحمله؟
من GitHub Actions للفرع `fix/scanner-cid-and-inventory-capacity` نزّل Artifact باسم `shadps4-readonly-smoke-<commit>`. افتح ZIP في مجلد مستقل، مع الحفاظ على البنية:
```
BREW00197/
  eboot.bin
  sce_sys/param.sfo
  sce_sys/icon0.png
  sce_sys/about/right.sprx
  sce_module/libc.prx
  sce_module/libSceFios2.prx
README_AR.md
SMOKE_MANIFEST.txt
EBoot_SHA256.txt
```
هذا **مجلد تطبيق مفكوك من نفس مصادر بناء الـPKG**، وليس لعبة أصلية ولا PKG يمكن تثبيتها على PS4. لا تعيد تسميته إلى CUSA عشوائية ولا تعدّل param.sfo.

## الخطوات على الكمبيوتر
1. استخدم `shadPS4` الرسمي و`QtLauncher` من https://shadps4.net/downloads/ . من `Version Manager` اختر نسخة حديثة مستقرة إذا كانت متاحة، أو بيئة اختبار حديثة. المرجع: https://shadps4.net/quickstart/ . **لم نثبت توافق تطبيقنا مع أي إصدار من المحاكي بعد.**
2. فك ZIP إلى مجلد تجريبي مستقل لا يحتوي ألعابك. شغّل **نواة shadPS4 (core)** بمسار مباشر إلى `eboot.bin`، وليس عن طريق خيار تثبيت `PKG` في `QtLauncher` (غير مثبت لتطبيقنا):
   - مثال Windows PowerShell بعد الانتقال لمجلد محاكي shadPS4 core:
     `./shadPS4.exe "C:\\PS4Test\\BREW00197\\eboot.bin"`
   - مثال Linux بعد الانتقال لمجلد المحاكي:
     `./shadPS4 /home/user/PS4Test/BREW00197/eboot.bin`
   - قد يكون اسم الملف التنفيذي ومكانه مختلفين حسب تنزيلك. لا تفترض أن `QtLauncher` نفسه يقبل وسيط مسار مباشر.
3. إن لم يظهر شيء، **احفظ سجل المحاكي** أولًا. راقب هل تم فتح `eboot.bin` وهل فشل Import أو Native VideoOut/Pad. لا تعدّل ملفات المحاكي أو تضع ملفات Firmware مجهولة المصدر لمجرد إصلاح التشغيل.
4. إن ظهرت الواجهة: تحقّق من Home ثم R1/L1 ثم Details ثم Downloads، واستخدم Circle للخروج. راقب الواجهة 30 ثانية للتأكد أن عدم الضغط على الأزرار لا يسبب إعادة رسم مستمرة، وانقل لقطات شاشة/Log.
5. **المتوقع:** عدم اكتشاف ألعاب الجهاز الحقيقي وعدم عمل GoldHEN FTP المحلي لأن المحاكي ليس PS4 عليه GoldHEN. ظهور `FTP_CONTROL_CONNECT_FAILED` أو قائمة ألعاب فارغة لا يُعتبر فشلًا في Discovery الخاص بالجهاز الحقيقي.
6. إذا وقع Crash: احتفظ بإصدار المحاكي، نظام التشغيل، SHA من `SMOKE_MANIFEST.txt`، وآخر 50 سطرًا من Log. لا تختبر أي Download/Install حقيقي.

## النجاح المطلوب
| البند | علامة النجاح |
| --- | --- |
| التشغيل | يظهر Home بوضوح بدون Crash |
| التنقل | جميع الأقسام الثمانية تستجيب لـR1/L1 |
| تفاصيل اللعبة | غير متوقعة دون بيانات حقيقية؛ لا نخترع لعبة تجريبية |
| الصور | Header والبطاقات والخلفية الزرقاء ظاهرة، وقد لا يوجد `pic1.png` |
| الخروج | لا يعلق التطبيق أو يستهلك الموارد بعد الرجوع |
| التشخيص | لا يصف أي فشل FTP بأنه تحديث متاح |
| جهاز PS4 14.00 | **NOT TESTED — لا يثبت هذا الاختبار التوافق الحقيقي** |

### قيود
المحاكي مبكر التطوير، وقد يفشل في دعم بعض رموز OpenOrbis/GoldHEN حتى لو كان الكود صحيحًا على PS4 حقيقي. لا تسجّل نجاح جهاز PS4 بناءً على نتيجة الشاشات في المحاكي. لا تجعل اختبار emulator-based أكثر مما تسمح به الأدلة.

**مراجع رسمية:** https://github.com/shadps4-emu/shadPS4 وhttps://shadps4.net/quickstart/ ، ويميز مصدر shadPS4 بين `shadPS4 <game-id>` وبين بدء ملف `/path/to/eboot.bin` مباشرة.
