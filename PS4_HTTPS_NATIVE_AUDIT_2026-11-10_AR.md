# تدقيق Native HTTPS — PS4 FW14 / GoldHEN
**تاريخ الفحص:** 10 نوفمبر 2026
**القرار:** DEFER — لا تفعيل للتنزيل على PS4 قبل إثبات Certificate Chain وHostname وStorage/Source Gates.
**نطاق الإثبات:** مراجعة Source Code وHeader Declarations، مع تجارب Host Python SSL. لا يوجد اختبار فعلي لـSceHttp/SceSsl على PS4 FW14.

## جدول الأدلة

| المسار / API | ما أثبته المصدر | ماذا لا نعرف | القرار |
|---|---|---|---|
| OpenOrbis samples/net_http | الكود يستخدم SceNet وSceSsl وSceHttp وsceHttpReadData لتنزيل مثال HTTPS | لم يقدم اختبار TLS موثقًا على FW14، والمثال يستخدم skipSslCallback تعيد HTTP_SUCCESS مهما كان verifyErr | لا تنسخ Callback |
| OpenOrbis OrbisHttpsCallback | يحتوي prototype على verifyErr وsslCert وcertNum وuserArg | معنى كل verifyErr، إشارات نجاح callback، وربط شهادات المتصفح بالاسم غير موثقين في هذا المصدر | لا نستنتج VERIFIED من callback |
| sceHttpsEnableOption / DisableOption | Header يصرّح بواجهات لتحديد flags | قيم/تأثير الـflags على FW14 لم تختبر على الجهاز | لا تستخدم Magic Bits كبرهان |
| sceHttpSetAutoRedirect | SDK Header يقدم Declaration غير مكتملة الوسائط (void prototype) | ABI الفعلي ومدى تعطيل Redirect مع إعادة استخدام Connection | لا تفعّل Adapter حقيقيًا |
| sceHttpSetRecvTimeOut | SDK Header Declaration بلا معاملات معرّفة | ABI والتوقيت وسلوك Timeout/Cancel على FW14 | مطلوب مراجعة ABI وHardware |
| sceHttpSetConnectTimeOut / sceHttpSetResolveTimeOut / sceHttpSetSendTimeOut | تصريحات بمعاملات id وmicroseconds | فعالية Deadline في DNS/Handshake/Body أو إرجاع الأخطاء | لا ادعاء Timeout مضمون |
| sceHttpAbortRequest | تصريح typed لطلب الإلغاء | هل الإلغاء يقاطع Read محجوبًا؟ وهل Cleanup آمن؟ | Hardware Gate |
| sceHttpGetAllResponseHeaders / sceHttpReadData | تصريحات Header وRead | ملكية ومهلة عمر Header Pointer، حدود الذاكرة ونتائج القراءة على FW14 | يجب إثباتها مع Bound Parsing |
| shadPS4 libSceHttp | الكود يعرض SSL flags وتعاملًا مع HTTP، لكنه يُبقي sceHttpsSetSslCallback STUBBED | سلوك PS4 الحقيقي مختلف ولا يمكن إثباته بالمحاكي | المحاكي ليس TLS Security Test |
| shadPS4 custom CA | Current emulator code bypasses certificate verification when sceHttpsLoadCert was called | لا يمثل ضمانًا لتصرف جهاز FW14 | تجنب الاعتماد على Emulator TLS |

## أخطاء أمنية تمنع التنفيذ من المصدر المرجعي

1. **Callback غير آمنة:** OpenOrbis net_http/main.c تعرّف skipSslCallback ترجع HTTP_SUCCESS حتى لو كانت verifyErr غير صفر. استعمالها قد يخفي رفض الشهادة. لا نستنسخها.
2. **Emulator ليس المرجع الأمني:** shadPS4 يكتب صراحة أن sceHttpsSetSslCallback "(STUBBED)"، وفي custom-CA path يجري تعطيل التحقق من شهادة الخادم. نجاح shadPS4 runtime لا يُحسب Verified HTTPS.
3. **SSL Flags ليست دليلًا:** shadPS4 http.h يذكر flags مثل SERVER_VERIFY / CN_CHECK / KNOWN_CA_CHECK / SNI وقيمها، لكن هذه تفاصيل تنفيذ للمحاكي؛ لم تُثبت على Firmware 14.00 ولا ينبغي نقلها كمتطلبات Production دون مصدر أصلي وتحقق Hardware.
4. **Timeout/Redirect واجهات غير محسومة:** بعض تصريحات OpenOrbis Http.h هي void بلا Prototype كامل. لا تُستدعى بوسائط مفترضة أو تُعتبر نجاحًا صامتًا. اتفاقية الاستدعاء/ABI لا تُستنتج من اختلاف shadPS4 مع SDK.

## ما طورناه مع إبقاء FW14 Fail-Closed

- Native C download_transport.c/h يراجع 200/206 وContent-Length وETag وRange وTLS facts منفصلة عن Header، ويمنع اكتمال Download عند فشل التحقق.
- DT_PORT_TLS_VERIFY_FAILED هو كود Adapter اصطناعي مستقل عن SDK؛ يستخدمه Host Python SSL فقط عندما ترفض شهادة فعلية بعد Certificate+Hostname Validation.
- DT_PORT_TIMED_OUT رمز Callback داخلي **ليس SceHttp error code**. يطبّق dt_begin/dt_read/dt_finish فشل Timeout مع Close، ويميز dt_cancel بين DT_ABORT_FAILED وDT_CLOSE_FAILED. لا يوجد وقت أو Socket داخل Native C Policy؛ يجب على الـBackend استخدام مهلات فعلية.
- اختبارات Host تغطي شهادات محلية صحيحة وخاطئة، Range/If-Range، رفض Redirect وإجابات غير سليمة، وCallback failure/Abort/Close. اختبارات HTTPS Slow Headers/Body تستخدم Python SSL وTimeout على Loopback.
- __ORBIS__ يعيد دائمًا DT_UNAVAILABLE من dt_begin، وPS4 download_file_backend.c لا يفعّل Storage. لا تستخدم هذه الوحدات كـDownloader إنتاجي.

## بوابات التفعيل الممنوعة الآن

1. عدم استدعاء SceHttp Native إنتاجيًا قبل شرح مثبت وقابل للتكرار لـTLS Certificate Chain + DNS Hostname Verification + SNI/CA flags.
2. إثبات إيقاف redirects/proxies، Timeout مفصول لـDNS/Connect/Send/Recv، والتعامل مع Cancel وResource Cleanup دون تجمد.
3. إثبات حدود Header/Body وعمليات Read في Firmware، وسلوك Error Codes.
4. PS4 File Backend صالح وآمن مع exclusive publish وfsync وفحص SHA-256 بعد الكتابة.
5. Authorized source وPKG/FPKG Compatibility وRights Gate، ثم Test Fixture فقط على PS4 الحقيقي في مرحلة Hardware Acceptance.

## أدلة أولية وروابط مراجعة

- OpenOrbis net_http sample: https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/blob/master/samples/net_http/net_http/main.c
- OpenOrbis Http.h: https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/blob/master/include/orbis/Http.h
- OpenOrbis callback typedef: https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/blob/master/include/orbis/_types/http.h
- shadPS4 libSceHttp: https://github.com/shadps4-emu/shadPS4/blob/main/src/core/libraries/network/http.cpp
- shadPS4 SSL flags (Emulator only): https://github.com/shadps4-emu/shadPS4/blob/main/src/core/libraries/network/http.h

**التحقق المطلوب مستقبلًا:** Host CI/OpenOrbis Compile/Link لا يثبتان Firmware Runtime أو TLS على Console. لا تعلن Complete قبل Hardware Evidence.
