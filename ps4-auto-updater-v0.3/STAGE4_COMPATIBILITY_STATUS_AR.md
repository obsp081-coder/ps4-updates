## تحديث 10 نوفمبر 2026 — تنفيذ Offline-first لربط Compatibility Engine

**القرار: MODIFY — استؤنف التطوير البرمجي؛ Hardware Verification مؤجل.**

- أصبح محرك `ui_compatibility.c` مربوطًا بالـOpenOrbis Build. الملفات التنفيذية لا تمنح أي صلاحية تنزيل أو تثبيت: `download_allowed=0` و`install_allowed=0` دائمًا.
- يوفّر `ui_source_publish_reviews()` جسرًا اختياريًا يستقبل **Candidate + Facts** متطابقتين في `CUSA` و`Content ID` و`Version`؛ عدم التطابق يرفض الدفعة كاملة دون تغيير بيانات UI السابقة.
- يُنقل تعارض مثبت إلى `UI_UPDATE_BLOCKED` مع `compat_blockers`، ونقص الأدلة إلى `UI_UPDATE_UNKNOWN` مع `compat_unknowns`. حتى `REVIEW_ONLY` يظل `UNKNOWN` ولا يظهر بوصفه تثبيتًا مرخصًا.
- الشاشة تستطيع إظهار رمز أول سبب حظر مع **إصدار المرشح الأول**، ولا تعمم الحكم على جميع الإصدارات أو جميع الألعاب.
- `ui_models.c` يرفض `BLOCKED` بلا `compat_blockers` ويواصل رفض `UI_UPDATE_VERIFIED`.
- اختبارات Host الجديدة تربط المصدر بالمراجع، وتغطي اختلاف Content ID وRetail/FPKG وFirmware والإصدارات والحالة غير المعروفة والرفض دون تغيير الحالة، وتختبر مسار العرض على Native Host Mock.
- **المتبقي:** إثبات مصدر تحديث مخوّل، بيانات Installed مستقلة عن SFO، تفسير جميع أسباب الحظر بالعربية مع اختيار إصدار محدد، بناء حالة تحديث حقيقية على FW14، وتنزيل/تثبيت آمن. كل ما سبق **NOT TESTED on real PS4**. إنتاج التطبيق يبقى **Read Only**؛ `ui_source_publish_reviews` ليس Provider حيًا.

**آخر Commit برمجي لهذا التغيير:** `a9eb64bfd6391439551e36336af21d0594a7573d`. يرجى مراجعة CI على هذا الـSHA قبل وصف البناء والاختبارات بالناجحة.

---

> **قرار جديد — 9 نوفمبر 2026:** هذه المرحلة **PAUSED / معلّقة بطلب المستخدم**. ملفات Compatibility Engine محفوظة ولا تُحذف. لم نربطها بعد بـNative UI، ولا نعدّها اختبارًا ناجحًا أو ميزة مكتملة. المرحلة النشطة حاليًا: **Stage 3 — Sources**، راجع `STAGE3_SOURCES_STATUS_AR.md`.

# المرحلة الرابعة — Compatibility Engine
**تاريخ التوثيق:** 9 نوفمبر 2026
**الحالة: قيد التنفيذ — ليست مكتملة وليست جاهزة للاستخدام.**
**آخر Commit يحتوي محرك التوافق:** `a1837d201e5b9c47457cfff431e57fcf76f7b60c`.

## ما تم إنجازه وحُفظ في GitHub
1. [6e6d9872](https://github.com/obsp081-coder/ps4-updates/commit/6e6d98728de42e1add9f0032a74e93dbc1e9298f) أضاف ملفَي `ps4/src/ui_compatibility.h` و`ps4/src/ui_compatibility.c`: محرك Native C نقي للقراءة فقط لتقييم *مرشحات* التحديث.
2. فحص حالات Title ID/CUSA وContent ID، اختلاف Region وPublisher، نوع Retail مقابل FPKG، حالة تثبيت اللعبة، إصدار اللعبة وإصدار التحديث، متطلبات Base Version، وأقل Firmware. يميز السبب بين `BLOCKED` (تعارض) و`UNVERIFIED` (دليل ناقص) و`REVIEW_ONLY` (مراجعة فقط).
3. جميع نتائج المحرك تُبقي `download_allowed=0` و`install_allowed=0`، ولا تعتبر أي توافق مؤكدًا.
4. [a1837d20](https://github.com/obsp081-coder/ps4-updates/commit/a1837d201e5b9c47457cfff431e57fcf76f7b60c) أصلح تعارض أسماء ثوابت `enum` في C.

## غير مكتمل — لا تدّعِ اجتياز الاختبارات
- **لم يتم ربط المحرك بعد** بملف `ps4/src/ui_models.c` أو بوظائف عرض PS4؛ تعديلات الربط أُعدّت في جلسة العمل لكن **لم تُحفظ في Commit**. لا تُعتبر جزءًا من الفرع.
- **لم تُضف اختبارات Host الخاصة بالمحرك بعد** ولم يُثبت نجاح Compile/OpenOrbis Build لهذه التغييرات. اختبارات 181/181 السابقة لا تخص هذه المرحلة.
- **لم يُختبر على shadPS4 أو PS4 الحقيقي** بعد إضافة محرك التوافق.
- لا توجد بيانات مصدر تحديثات موثوقة، ولا Installed-state مستقلة عن SFO، ولا مصدر تحقق حقيقي لتوقيع/نوع حزمة FPKG؛ معلومات `source_authorized` و`integrity_verified` هي إدخالات للمنطق فقط، وليست إثباتًا تشفيريًا.

## الخطوة التالية
1. ربط المحرك بـ`ui_models` بشكل محافظ ومنع تمرير `UI_UPDATE_VERIFIED` مباشرة من بيانات غير موثقة.
2. إضافة اختبارات فشل صريحة لـ CUSA غير مطابق، Content ID خاطئ، Retail/FPKG مختلط، الإصدار الأقدم، Firmware أعلى، Base Version غير مطابق، وانعدام دليل التثبيت أو المصدر. حتى حالة `REVIEW_ONLY` لا تتيح تنزيلًا أو تثبيتًا.
3. تشغيل Host Tests وOpenOrbis CI على آخر SHA، ثم اختبار نسخة Read-only على shadPS4 عند الحاجة.
4. لا تُفعّل Download/Install حتى تنتهي بوابات الإثبات على جهاز حقيقي.

**قرار المرحلة: MODIFY — مواصلة التنفيذ والاختبار، لا إعلان اكتمال.**
