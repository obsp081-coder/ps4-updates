# قرار التنفيذ: تطوير Offline-first وتأجيل Hardware Validation

**التاريخ:** 10 نوفمبر 2026  
**الحالة:** خطة التنفيذ الجديدة المعتمدة بناءً على طلب المستخدم.  
**المستهدف:** PS4 FW 14.00 + GoldHEN. التوافق غير مفترض قبل اختبار العتاد.

## القرار الهندسي: MODIFY

نستمر في بناء الوحدات المستقلة وتجربتها على Host وshadPS4 دون انتظار جهاز PS4.
نؤجل **Hardware Validation** فقط، لا اختبارات الوحدات ولا مراجعة الأمان.
أي ميزة تعتمد على صلاحيات Firmware أو مصدر PKG أو حالة تثبيت حقيقية تبقى
**Disabled / Unverified** حتى توفر دليل على جهاز حقيقي. لا تُعرض نتائج Mock
أو تجربة shadPS4 كدليل PS4 FW14.

## تسلسل التنفيذ ومخرجاته

| مرحلة | الأعمال التي يمكن إنهاؤها الآن | الدليل المطلوب الآن | ما يؤجل للعتاد |
|---|---|---|---|
| 2 — Library | FTP/SFO parser، كشف الاختلاف، تصنيف base/patch/DLC، Cache، UX للخطأ وUnknown | Host integration + malformed inputs + CI | قراءات SFO الحقيقية، External، Installed proof، GoldHEN وفيرموير فعلي |
| 3 — Sources | مراجعة Provider contracts وLegal provenance، Registry وRead-only candidate view | Offline fixtures + valid/invalid catalog tests | Live sources، إذن التنزيل، الشبكة على PS4 |
| 4 — Compatibility | Pure native C assessor، أسباب BLOCKED/UNVERIFIED، Fail-closed integration | Host C regression، OpenOrbis build | PKG/FPKG pairing الحقيقي، FW الحقيقي، توقيع/سلامة الحزمة |
| 5 — Downloader | تصميم Queue/Cancel/Retry/Resume وSpace/TLS/Hash، واختبار Synthetic authorized HTTP fixtures فقط | Mock HTTP، interruption، checksum، disk-full tests | اختبار نقل فعلي على PS4، صلاحيات التخزين، مصدر تنزيل مخوّل |
| 6 — Installer | Interface/State-machine، تحقق قبل التنفيذ، Errors وCleanup policy، *بدون استدعاء تثبيت حقيقي* | Unit Tests وMocks لا تغيّر جهازًا | صلاحيات Install API ونتيجة تثبيت حقيقي على FW14 |
| 7 — Stability | اختبارات الانحدار والتكامل، Memory bounds، Emulator smoke، مراجعة الأداء بالـHost | CI على SHA محدد، شاشات Native Host وshadPS4 | الاختبار النهائي على الجهاز، قياسات الأداء، تثبيت PKG واختبار failure/recovery |

## القواعد
- مستويان منفصلان: **CODE/Host Complete** ليس **PS4 Verified**.
- لا تستعمل لعبة أو Update وهميًا في إنتاج التطبيق. Fixture/MOCK داخل الاختبارات فقط.
- لا تنزيل إلا من مصدر مصرح به؛ metadata عبر HTTP وحدها لا تثبت أحقية تنزيل PKG.
- لا تثبيت ولا حذف PKG قبل نجاح مثبت في العتاد، مع بقاء Save Data خارج نطاق التطبيق.
- لا تثبت Firmware مختلفًا ولا تبدأ تحديث نظام PS4 بسبب التطبيق.
- أبقِ `download_enabled=false` و`install_enabled=false` إلى تحقق كل بوابات السلامة.
- اختبارات Host مستمرة مع كل Commit؛ جمع جميع أخطاء الكود إلى النهاية مرفوض.

## نقطة التوقف الحالية
المرحلة 2 لم تثبت قراءة لعبة حقيقية وحالتها المثبتة. نكمل باقي الوحدات برمجيًا الآن
مع الحفاظ على `UNKNOWN` و`Read Only` حيث ينقص دليل.
Stage 4 pure native code أصبح هدف التنفيذ المباشر: اختبار الوظيفة وربطها بالبناء
قبل أي دمج في الواجهة. بعد اكتمال مراحل Host، يُجرى Hardware Acceptance بالترتيب
2 → 3 → 4 → 5 → 6 → 7؛ أي فشل يوقف تفعيل المرحلة المعنية ولا يمحو ما سبق.
