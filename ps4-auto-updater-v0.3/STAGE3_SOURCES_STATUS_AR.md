## تحديث المرحلة الثالثة — 9 نوفمبر 2026: Source Bridge داخل Native UI

- ربطنا نموذج البيانات الأصلي بـ`UiSourceCandidate` محافظ لا يسمح بتحويل Retail HTTP أو FPKG submissions إلى تحديثات متوافقة، وأبقينا `Download/Install` مغلقَين. سجلات المصادر المختلفة لا تُدمَج إذا تساوى CUSA/Version لكن اختلف المصدر؛ المعلومات غير المتاحة تظهر فارغة.
- ملفات المصدر: `ps4/src/ui_source_bridge.h/.c` و`ps4/src/ui_models.h/.c` و`ps4/src/scanner_ui.cpp`. Host Mock فقط يغذّي بيانات اصطناعية؛ لا يوجد اتصال Internet أو Sony أو FPKG Provider على PS4 حتى الآن.
- كود المرحلة الرابعة ما زال PAUSED وغير مربوط. راجع `STAGE3_SOURCE_CONTRACT_AR.md` لبوابات ثقة المصدر وخطة العمل الآمنة.
- اختبارات الدفعة الجديدة: [Host run](https://github.com/obsp081-coder/ps4-updates/actions/runs/42699845536) و[Native build](https://github.com/obsp081-coder/ps4-updates/actions/runs/42699837258) — لا تُعتبر ناجحة حتى اكتمال CI.

---

# المرحلة الثالثة — مصادر تحديثات PS4
**الحالة:** ACTIVE / PARTIALLY VERIFIED (Offline only).
**المرحلة الرابعة:** PAUSED بطلب المستخدم؛ ملفاتها محفوظة، ولم تُدمج في تطبيق PS4 أو تُختبر في هذا التغيير.

## الدليل والمصادر
- توثيق PS4 Developer Wiki لمسار PS4 Retail patch XML: https://www.psdevwiki.com/ps4/Online_Connections
- مشروع andshrew PS4-Updates-Python (MIT) يقول إن واجهته توفر معلومات **أحدث إصدار فقط**، ولا ينزل التحديثات: https://github.com/andshrew/PS4-Updates-Python
- ItemzFlow يقدّم Retail Game Updates للألعاب Retail المرخّصة، **لا يستنتج توافق FPKG**: https://github.com/LightningMods/Itemzflow
- هذه **أدلة وجود بيانات/مسارات**، وليست إذنًا قانونيًا عامًّا بأتمتة التنزيل، ولا تحققًا من ملاءمة PKG على GoldHEN 14.00.
- نقطة Sony الموثقة تاريخيًا تستخدم **HTTP غير موثّق**؛ البيانات منها لا يمكن أن تمنح صلاحية تنزيل أو تثبيت. حتى 404 في تقرير lookup لا يثبت عدم وجود تحديث من كل المصادر.

## ما أُنجز وحُفظ
1. [b6000c0f](https://github.com/obsp081-coder/ps4-updates/commit/b6000c0fbeb0d17bb2426bc1c71c1955a40a666e): تعديل `official_patch_lookup.py` لحفظ **فقط** نسخ Retail المذكورة بالفعل في رد XML الواحد، بدل أحدث نسخة فقط، مع سقف 128 عنصرًا ورفض DTD/ENTITY وتكرار هوية الحزمة، ودون اختراع سجل نسخ تاريخي أو حجم أو Firmware غير متوفر.
2. [3564073c](https://github.com/obsp081-coder/ps4-updates/commit/3564073cc44abf218716357d6e9a7fc87395c36b): `update_source_registry.py` يجمع **Offline** تقرير Retail من `official_patch_lookup.discover` وترشيحات FPKG من `catalog_ingest.ingest` دون خلط النوعين أو نقل `download_url` إلى النتائج. يفحص CUSA/Content ID/Version والسماح بالطرف المتلقي ويرفض العناوين المكررة والإصدارات المتناقضة ويحدد `truncated` إذا تجاوز 24 ترشيحًا للعبة.
3. [c212e49e](https://github.com/obsp081-coder/ps4-updates/commit/c212e49ef3054d33ed70664e10f58fea53d57575): قبول حقل `auto_install: false` السلبي الصادر من أداة التطبيع، مع استمرار منع أي قيمة تفعّل العمليات.
4. [CI Host tests #42688083131](https://github.com/obsp081-coder/ps4-updates/actions/runs/42688083131): **198/198 PASS**. [Native CI #42688083093](https://github.com/obsp081-coder/ps4-updates/actions/runs/42688083093): **PC Simulation 198/198 PASS + OpenOrbis PS4 Package Build PASS**. هذه اختبارات بناء/Host؛ **ليست** شاشات تحديث فعلية من Sony ولا اختبار مصادر على PS4.
5. هذا العمل **لم يُفعّل أي Network Provider على PS4 نفسه**. ملف Python أداة تطوير/مراجعة لا تعمل داخل PS4، ومخرجاتها الحالية غير موصولة بعد بـ`UiUpdateInfo`.

## ما لم ينجز — بوابات قبول المرحلة الثالثة
- العثور على مصدر Retail **مرخّص/مصرح به** وقابل للتحقق الأمني؛ البيانات الحالية من HTTP غير موثّق ومحدودة بالإصدارات المذكورة في الرد فقط.
- لا يوجد كتالوج FPKG موثوق وشامل ولا مصداقية مثبتة لتوافق الحزم مع نسخ الألعاب.
- لا يوجد إثبات Live على PS4 لتوافر Metadata Version/Size/Firmware من كل مصدر؛ القيم المفقودة يجب أن تبقى Unknown.
- لم يبنَ بعد `Native PS4 provider` للشبكة/الكتالوج مع TLS موثّق، Limits، Timeout، Cancel وعدم تنزيل تحديث النظام. لا تَعِد بوظيفة كاملة حتى اكتمال هذه البوابات.
- `Download/Install` ما زالا **OFF**، ولا تعطينا المرحلة الثالثة وحدها حق تفعيله.
- مصدر صورة اللعبة `pic1.png` منفصل عن مصدر تحديثات اللعبة؛ ليس ضمن نطاق المرحلة الثالثة الحالية.

## الخطوة الهندسية التالية
1. مراجعة شروط استخدام مصدر Metadata بديل موثوق وآمن ومصرّح به، وتحديد `SourceContract` (الأصل، إصدار Catalog، الحق، النقل، الحجم الأقصى، الـTTL، حالة الفشل).
2. بناء Provider `Read-only` بحدود واضحة بعد اعتماد المصدر، مع نتائج `NOT_LISTED / LOOKUP_FAILED / UNVERIFIED` منفصلة؛ لا تصنف أي إصدار على أنه متوافق.
3. تمرير نتائج المصدر إلى Native UI بشكل `Unknown/Blocked` دون تفعيل Download/Install، ثم اختبار محاكي وبعده جهاز FW14 GoldHEN.

**قرار المرحلة الثالثة: MODIFY — الأساس Offline جاهز، المصدر المصرح والربط Native والاختبار على الجهاز ما زالت مفتوحة.**
