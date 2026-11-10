## تحديث 10 نوفمبر 2026 — HTTPS Verified Host Fixture مع Native C Transport Policy

**Verdict: MODIFY — إثبات TLS حقيقي على Host فقط، وبقاء PS4 Native Downloader مغلقًا.**

**Code HEAD:** 3b781e3963f84d013e0736ad1dc66dbd7597cf24
**Host CI:** 254/254 PASS على Code HEAD — https://github.com/obsp081-coder/ps4-updates/actions/runs/42835168317
**OpenOrbis CI:** PC Simulation PASS وBuild PS4 Package PASS على Code HEAD — https://github.com/obsp081-coder/ps4-updates/actions/runs/42835162775
**Real PS4 FW14 + GoldHEN:** لم يجر اختبار على الجهاز؛ لا تعتمد نجاح Compile كدليل Runtime.

### Code Written / Host Tested

- تمت إضافة tests/test_download_https_verified.py: اختبار مستقل يستعمل OpenSSL لإنشاء CA محلية مؤقتة وشهادة Server موقعة مع DNS SAN باسم updates.example. خادم HTTPS مقيّد على 127.0.0.1، ولا يجري أي Public DNS أو تنزيل تحديثات ألعاب. عميل Python ssl يستخدم CERT_REQUIRED وcheck_hostname مفعّلًا، ويفرض TLS 1.2 أو أحدث، ويتحقق من الشهادة والاسم عبر Handshake حقيقي.
- الاختبار يمرّر HTTP response headers والبايتات الناتجة عن TLS المتحقق منه إلى نفس Native C download_transport.c عبر ctypes callbacks. هذا ليس Mock لنجاح TLS؛ الشهادة والحزمة TLS حقيقيتان محليًا، بينما C policy هو الذي يراجع 200/206 وContent-Length وRange/ETag والـEOF.
- الاختبارات تؤكد نجاح نقل البيانات عند CA وHostname صحيحين، ونجاح Resume GET مع Range/If-Range مطابقين، ورفض CA غير موثوقة وHostname غير صحيح، ورفض Redirect وContent-Range/ETag غير المطابقين و200 بدل 206، وعدم إعلان نجاح الاستجابة المقطوعة. HTTPS Host لا يعني أن Native PS4 يقوم بتوثيق الشهادات.
- تمت إضافة DT_PORT_TLS_VERIFY_FAILED إلى ps4/src/download_transport.h، وتعديل download_transport.c لتمييز فشل Chain/Hostname Authentication من فشل الشبكة العادي. أي فشل أثناء open_headers يستدعي close_request مع فحص نتيجة الإغلاق. أضيفت حالات منفصلة إلى tests/ui_mocks/transport_policy_smoke.c وأصبح test_download_https_verified.py يختبر نفس Error Code بعد TLS Verification Failure حقيقي.
- لا يتم قبول TLS Authenticated Fact من HTTP Headers؛ يتحكم فيها Adapter مستقل بعد Handshake. في بناء PS4 ما زالت dt_begin() تحت __ORBIS__ تعيد DT_UNAVAILABLE فورًا، دون Socket أو Callback أو كتابة ملف.

### Source Review / Unknowns

- مثال OpenOrbis net_http يستخدم skipSslCallback التي تعيد HTTP_SUCCESS حتى مع verifyErr؛ لا يصلح إثبات Chain أو Hostname Verification ولا ينبغي نسخها في Production. https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/blob/master/samples/net_http/net_http/main.c
- يعرّف OpenOrbis Http.h واجهات sceHttpsEnableOption وsceHttpsSetSslCallback وsceHttpAbortRequest وغيرها، وتعرّف orbis/_types/http.h نوع OrbisHttpsCallback مع verifyErr. هذه Definitions/Declarations فقط؛ لم يتم إثبات bit flags أو سلوك SSL certificate/hostname في FW14 على Hardware، لذلك لم نبن Native SceSsl Backend غير موثق.
- Host integration يعتمد على Python ssl/OpenSSL وليس SceSsl؛ ولا يغطي PS4 Permissions، Storage No-overwrite، Hostname Validation في Firmware 14.00، أو TLS timeout/cancel في Native API.
- ما زالت Sources وFPKG Compatibility وInstaller وAuthorized Download Gate غير مكتملة. لا نشر تحديثات ألعاب محمية، ولا Download/Install على PS4. PR #1 يبقى Draft ولا Release نهائي.

**الخطوة القادمة:** تدقيق Native SceHttp/SceSsl لإيجاد Certificate Chain + Hostname Evidence قابل للاختبار دون Callback تتجاوز verifyErr؛ وعزل واجهة الأخطاء والوقت/إلغاء النقل مع Host regression tests. قبل FW14 Hardware Proof تبقى جميع بوابات تنزيل/تثبيت PS4 مغلقة.

---

## تحديث 10 نوفمبر 2026 — Host Recovery V2 وTransport Gate (أحدث Code)

**Verdict: MODIFY — Host recovery verified by tests; Native PS4 Download/Install remain disabled.**

- **Code SHA:** 9412120133a779415dd18a252af0d2c4840c77cc
- **Host CI:** **245/245 PASS** — https://github.com/obsp081-coder/ps4-updates/actions/runs/42832732548
- **OpenOrbis:** **PC Simulation PASS + Build PS4 Package PASS** على Code SHA نفسه — https://github.com/obsp081-coder/ps4-updates/actions/runs/42832732399
- **shadPS4:** لم يُستخدم لهذه الخطوة Pure C/Host Storage.
- **Real PS4 FW14:** لم يُختبر على هذا الإصدار. Release Gate لا يزال مغلقًا.

### التغييرات البرمجية المثبتة

1. **Host Resume recovery:** تم تعديل tests/host_fixture_resume.py لإضافة Checkpoint schema version 2 (part_dev / part_ino) وربطها بملف .part الفعلي، مع رفض Duplicate JSON keys وschema التالف. تتم مراجعة Source/Content ID/Version/Trusted Full SHA-256 وHash الجزء المحفوظ قبل قبول الاستكمال. إذا كان الملف أطول من Checkpoint Offset نتيجة Crash/انقطاع متكرر، يُقص **الذيل غير المعتمد فقط** باستخدام FD للـInode المتحقق منه، مع fsync، ثم يُعاد HTTP Range من Offset الموثوق.
2. **Crash after publish:** إذا اكتمل .part وكانت Full SHA-256 مطابقة، يمكن نشره حصريًا دون إعادة طلب البايتات. وإذا حدث Crash بعد hardlink النهائي وقبل Cleanup، يتحقق Restart من final الموجود ويحافظ عليه دون Replace أو Delete. تتحقق الدوال قبل Cleanup من device/inode حتى لا تحذف Leaf تم استبداله.
3. **Concurrency policy:** ملف flock ثابت يمنع تنفيذ Resume متزامن بين Host fixture workers المتعاونين؛ لا يُحذف Lock كي لا ينشأ unlink/recreate race. هذا لا يضمن الحماية من عمليات خبيثة أخرى تملك صلاحيات UID نفسها، ولا يمثل PS4 File Backend.
4. **Host tests:** الاختبارات الجديدة تغطي تكرار HTTP disconnect، Disk Full بعد Partial Write، Crash بعد Link وقبل Cleanup، تبديل Inode، فساد Prefix مع Tail إضافي، Duplicate/Incomplete Checkpoint، Symlink Lock، وحماية ملف مستبدل أثناء Cleanup.
5. **Native C transport contract:** أضيف ps4/src/download_transport.h/.c وربط download_transport.o في ps4/Makefile، واختبارات tests/test_download_transport.py وtests/ui_mocks/transport_policy_smoke.c. يفرض حدود Headers 8192 bytes وRead chunks 64KiB، ورفض HTTP غير المشفر، TLS facts غير المثبتة، 200/206 غير المطابق، Redirect وRange/ETag الخاطئ، EOF المبكر والبايتات الزائدة، مع إغلاق Session عند الخطأ. **كل حقائق TLS الإيجابية في الاختبارات SYNTHETIC فقط؛ لا يوجد TLS live adapter.** على __ORBIS__ تعيد dt_begin دائمًا DT_UNAVAILABLE دون استدعاء Callback.
6. **CI correction:** كشف أول CI لـTransport خطأ Conditional Compilation: كانت دالة stop محصورة داخل !__ORBIS__ مع استدعاء من dt_finish. تم إصلاحه في Code SHA أعلاه، وإعادة CI حتى PASS لعدد 245 Host tests وOpenOrbis Package Build.

### مراجعة مصادر OpenOrbis — حدود الإثبات

- مثال OpenOrbis net_http يستدعي SceNet/SceSsl/SceHttp وsceHttpReadData، لكنه يعرّف skipSslCallback تعيد SUCCESS بغض النظر عن verifyErr. **هذا مثال اتصال، وليس دليلًا على Certificate Chain/Hostname Validation آمن.** https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/blob/master/samples/net_http/net_http/main.c
- Header Http.h يحتوي تصريحات HTTP، SSL callback، Connect/Resolve/Send Timeout، Abort/Read/Headers، لكن مجرد Declaration أو Sample لا يثبت سلوك Firmware 14.00، ولا يسمح بتفعيل Network backend. https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/blob/master/include/orbis/Http.h
- Header Ssl.h يتضمن تصريحات كثيرة غير محددة التواقيع؛ لا نستخدمها كـAPI مثبتة دون تحقق مستقل. https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/blob/master/include/orbis/Ssl.h

### القيود والقرارات الأمنية

- هذه تجربة **Host-only loopback HTTP** مقيدة بـ127.0.0.1؛ لا Transport HTTPS فعليًا، ولا صلاحيات PS4 Storage، ولا Native Resume/Downloader مفعّل.
- Checkpoint V2 الحالي **لا يدعم Migration تلقائيًا من V1**؛ الملفات القديمة أو Metadata غير المتطابقة تُرفض وتحفظ للمعاينة بدل التخمين. لا يوجد ضمان Crash-safe عام ضد كل Power Loss أو TOCTOU لنفس UID.
- No-overwrite Publish على FW14، شهادات TLS، مصدر FPKG مصرح به، فحص PKG/FPKG الحقيقي، Installer/Progress كلها **غير مثبتة على PS4 الحقيقي**.
- أبقِ PR #1 Draft، ولا تنشر PKG نهائيًا، ولا تفتح Download/Install قبل وجود حقوق المصدر وTLS Verified/Storage/Compatibility/Hardware Gate.

**التالي:** إثبات شهادة HTTPS وسلوك Hostname Verification مع SceHttp/SceSsl أو بديل مثبت؛ ثم Native Adapter آمن مع Test Fixture مخوّل، واستكمال Storage/Installed-state Hardware Tests عند توفر الجهاز.

---

## تحديث 10 نوفمبر 2026 — Resume Checkpoint + HTTP 206 حقيقي على Host

**Verdict: MODIFY — Host Resume V1 + Native C Policy code reviewed/tested، لكن PS4 Download/Install ما زالا معطلين.**

**Code SHA:** `7e5f572dea9541779ccdd79fbc669b534df59576`  
**Host CI:** **234/234 PASS** — https://github.com/obsp081-coder/ps4-updates/actions/runs/42825272867  
**OpenOrbis Build:** **PASS** — https://github.com/obsp081-coder/ps4-updates/actions/runs/42825265008

### المنجز
- `ps4/src/download_resume.h/.c` يعرّف `DlResumeCheckpoint` و`dl_resume_review()`: يرفض استكمال ملف إلا بعد مطابقته مع **Content ID + Version + حجم الملف + SHA-256 الكامل من Manifest موثوق منفصل**، ومطابقة **حجم الملف الجزئي الفعلي وبصمته** مع Checkpoint، ووصول `206` بمدى صحيح وStrong `ETag` مطابق عبر `dl_http_parse_headers` و`dl_http_review`.
- Trust bit الخاص بـTLS وحقائق `Range/If-Range` تأتي من Adapter منفصل لا من استجابة الخادم. **لا توجد شهادة TLS محققة أو Adapter PS4 الآن**؛ اختبارات Host تستخدم حقيقة TLS اصطناعية *فقط لاختبار القرار*.
- `tests/host_fixture_resume.py` و`tests/test_host_fixture_resume.py`: ينشئان خادم TCP/HTTP محليًا في `127.0.0.1`، ويكتبان `.part` وCheckpoint مستقل بعد `fsync`، ثم ينفذان `GET Range: bytes=OFFSET-` مع `If-Range: ETag`، ويتحققان من البايتات قبل الاستكمال ثم من SHA-256 النهائي بعد النقل وينشران الملف عبر `link` الحصري داخل مجلد Host خاص.
- `tests/native_resume_gate.py` يحمّل **نفس دالة C** داخل Host Test عبر `ctypes` ويمرر لها الـCheckpoint وترويسات HTTP `206` المستلمة فعلًا قبل Append. هذه مشاركة فعلية لسياسة Native، **وليست Native Network Transport أو PS4 Runtime Test**.
- تم اختبار نجاح الاستكمال، ورفض `ETag` المتغير و`200` بدل `206` و`Content-Range` خاطئ أو مكرر، ورفض ملف جزئي معدّل، ورفض Manifest/Checkpoint مختلفين، ورفض قطع الاتصال أو فساد البايتات، وحماية الملف النهائي القديم و`.part` الموجود سابقًا.
- `download_resume.o` مرتبط في بناء OpenOrbis للاختبار البرمجي فقط. ما زال `download_pipeline.c` يبدأ من Byte 0 على Host و**يرفض التشغيل بالكامل في `__ORBIS__`**. لا توجد أوامر حقيقية لتنزيل أو تثبيت تحديثات الألعاب.

### القيود والبوابات التالية
1. **Resume Host V1 تجربة محكومة وليست استكمالًا دائمًا لكل الأعطال**: يتم إنشاء Checkpoint عند توقف مخطط ومدوّن. إذا انقطع النقل *أثناء الاستكمال* وتغير طول `.part`، يرفض النظام محاولات إعادة الاستكمال تلقائيًا لأن Checkpoint أصبح قديمًا، ويستلزم Recovery/Reset صريحًا. هذا أفضل من قبول بايتات غير موثوقة.
2. `Checkpoint` في الاختبار مكتوب كـJSON تحت Host POSIX، لا توجد صيغة Persistence معتمدة على PS4 ولا اختبار للتعافي بعد تعطل الجهاز أو انقطاع الكهرباء. يجب فحص استعادة Ownership وSymlink/Race والـDisk Reserve قبل التفعيل.
3. `HTTPS/SceSsl` وCertificate chain + hostname validation وTrusted/Authorized source catalog غير مثبتة. `HTTP` المحلي غير المشفر لا يثبت أيًا منها.
4. لا يوجد **No-overwrite Publish** مثبت على FW14 + GoldHEN، لذا Native PS4 Storage Backend مقفل. لا تفعّل `Download` أو `Install` أو تقدم UI إلا بعد Transport + Storage + Compatibility Hardware Gates.
5. الاختبار الحقيقي على PS4 مؤجل لمرحلة القبول النهائي بطلب المستخدم. استخدم `shadPS4` فقط عند الحاجة لاختبار Native runtime؛ لا تدّعِ اختبارات غير منفذة.

**الخطوة العملية التالية:** تصميم `PS4 HTTPS TransportAdapter` ضمن واجهات SDK المعروفة، مع آلية Cert Validation مؤكدة، ومزيد من اختبارات Restart/Crash/Low Storage للـCheckpoint على Host. المسار الحالي Read Only.

---

## تحديث 10 نوفمبر 2026 — Download Pipeline V1 عبر Host TCP Loopback

**القرار: MODIFY — دمج مسار تنزيل C حقيقي في اختبارات Host فقط، ورفض التشغيل على PS4 عمدًا.**

**Code SHA:** `7e2e323952dad311b7179e621e7a23ff4be3e7e7`  
**Host CI:** **222/222 PASS** — https://github.com/obsp081-coder/ps4-updates/actions/runs/42819573731  
**OpenOrbis Package Build:** **PASS** على Code SHA نفسه (Compile + PKG structure validation؛ لم يتم نشر Release) — https://github.com/obsp081-coder/ps4-updates/actions/runs/42819573705.

### المنجز
- `ps4/src/download_pipeline.h/.c`: جمع `download_http_headers` ثم `download_http_policy` ثم `download_queue` ثم `download_storage` مع SHA-256 للملف المخزن و`publish_exclusive`. يُقبل Start فقط بعد Parser وPolicy وSpace Preflight. Progress هو عدد البايتات الفعلية التي أكّد `write_all` كتابتها.
- `dlp_feed()` يمنع تجاوز Content-Length المتوقع أو Chunk يتجاوز 64 KiB؛ `dlp_finish()` يرفض بيانات ناقصة أو بصمة خاطئة ولا يحوّل Queue إلى COMPLETED قبل تحقق التخزين والنشر. `dlp_cancel()` ينظف الملف الجزئي المملوك للعملية فقط. أخطاء Cleanup تُعرض كأخطاء ولا تُخفى.
- التجربة End-to-End: `tests/ui_mocks/pipeline_loopback_client.c` تنشئ Socket TCP حقيقيًا متصلًا فقط بـ`127.0.0.1`، وتغذي C Pipeline باستجابة HTTP/1.1 وبايتات هيئة اختبار محلية. `tests/test_download_pipeline_loopback.py` يشغل `ThreadingHTTPServer` محليًا؛ ويقدم Hash مستقلًا من Python hashlib، ثم يفحص الملف النهائي وبصمته. تشمل الاختبارات إلغاء حقيقي بعد استلام بايتات، ملف مقطوع/معدل/أطول، تكرار Header وRedirect و206 غير متوقع، ورفض Trust غير المثبت.
- **TLS اصطناعي داخل Host Fixture فقط:** لأن Loopback يعمل بـHTTP غير مشفر، يُمرر وضع `tls-test` في برنامج Host ليختبر منطق Pipeline، **ولا يعني اتصال HTTPS مُصادق عليه**. `__ORBIS__` في `dlp_begin()` يعيد `DLP_UNAVAILABLE` دائمًا مهما كانت قيمة TLS أو Header؛ و`download_file_backend` الخاص بـPS4 لا يسمح بعمليات ملفات.
- `tests/ui_mocks/pipeline_ps4_gate_smoke.c` و`tests/test_download_pipeline_ps4_gate.py` يجمّعان مسار `__ORBIS__` على Host ويثبتان أن Queue لا تبدأ ولا تُكتب أية بايتات. هذا **ليس اختبار Hardware أو Emulator**.
- كشف اختبار End-to-End عدم تطابق بروتوكول خادم Python الافتراضي `HTTP/1.0` مع Parser الذي يدعم فقط `HTTP/1.1`؛ صُحح اختبار الخادم، ثم نجحت الاختبارات.

### محدوديات واضحة / بوابات مفتوحة
1. **لا توجد Native PS4 SceHttp/SceSsl Connection** أو شهادة TLS موثقة، ولا مزود تحديثات ألعاب مصرح به. لا يُسمح للتطبيق بجلب أي تحديث PKG فعلي.
2. **Resume بعد الانقطاع غير منفذ**؛ Pipeline V1 يبدأ من Byte 0 ويرفض دائمًا `resume_offset != 0` من خلال عدم تقديم مسار استكمال. اختبارات `206` تخص طبقة Policy فقط، وليست Resume E2E.
3. **لا نشر آمن مثبت على FW14**؛ يبقى PS4 Storage Backend مقفلًا لأنه لا يجوز افتراض `linkat` أو اعتبار `sceKernelRename` بديلًا آمنًا لـNo-overwrite.
4. Progress يصل إلى C Queue من الكتابة الفعلية في Host؛ لا يوجد بعد ربط لواجهة PS4 أو Background Download، ولا Status Download حقيقي على شاشة السوني.
5. **اختبار PS4 الحقيقي مؤجل للنهاية** بطلب المستخدم؛ `shadPS4` سيستخدم فقط عندما توجد Native Runtime API تستحق التجربة. لا يوجد Hardware claim جديد.

**المهمة التالية:** تصميم `TransportAdapter` مع Trust Contract حقيقي للشهادات وسلوك Range/ETag، ثم اختبار الاستكمال End-to-End على Host باستمرارية وحماية ضد تبدل المصدر أو ملف .part؛ لا تفعّل ذلك على PS4 قبل إثبات Exclusive Storage & TLS.

---

## تحديث 10 نوفمبر 2026 — HTTP/1.1 Header Decoder Native وتثبيت بوابة أمان التخزين

**Verdict: MODIFY — Native parser / Host tests / OpenOrbis PASS؛ PS4 Download/Install ما زالا مغلقين.**

**Code SHA:** `8edf524244108de212047fa0b4ea3f363e2a4a31`  
**Host CI:** **217 / 217 PASS** https://github.com/obsp081-coder/ps4-updates/actions/runs/42810242238  
**PS4 Package Build:** **PASS** https://github.com/obsp081-coder/ps4-updates/actions/runs/42810234495

### ما أُضيف
- `ps4/src/download_http_headers.h/.c` Decoder صارم لـ**HTTP/1.1 response header block فقط**، محدود بـ8 KiB، 1024 بايت في كل سطر، و64 حقلًا؛ لا يتعامل مع HTTP body أو HTML ولا يجري اتصال شبكة.
- يستخرج `Content-Length` و`Content-Range` و`ETag` وأعلام `Transfer-Encoding` و`Content-Encoding` و`Location`. يرفض التكرارات الحساسة، وInteger Overflow، وHeaders غير ASCII أو `obs-fold`، والسطور/البنية غير الصحيحة. إن فشل فلا يعيد حقائق جزئية للمستخدم.
- ربطه الاختباري بـ`dl_http_review()` يختبر `200` من الصفر و`206` بعد استكمال Range/If-Range مع Strong ETag مطابق، مع رفض اختلاف الإزاحة والطول والبصمة/المصدر.
- لا يستطيع Header مزور باسم `X-TLS-Authenticated` تفعيل `tls_authenticated`: القيمة تبقى 0 حتى يقدم Adapter موثوق دليل HTTPS حقيقيًا. الاختبارات تستخدم قيمة TLS اصطناعية **للمراجعة فقط**.
- `tests/ui_mocks/http_headers_smoke.c` و`tests/test_download_http_headers.py` يغطيان Duplicate `Content-Length/Range/ETag`، وMalformed/Truncated Header، وOverflow، وUnsupported Encoding وRedirect، وTLS spoofing. **Host tests: 217/217 ناجحة**.
- رُبط الـDecoder ببناء OpenOrbis، ولا توجد أي عملية تحميل تلقائي أو تطبيق تحديث نظام PS4، ولا أعمال ملفات على PS4.

### قرار PS4 storage publish — DEFER
- لا دليل Code/Hardware كافي يثبت No-overwrite atomic publish على FW14. `OpenOrbis/musl` يستخدم مسارات قد تعيد `ENOSYS` لـ `linkat/unlinkat` في بناء PS4، كما أن `sceKernelRename` وحده لا يضمن منع استبدال ملف موجود. الروابط الأولية:
  - https://github.com/OpenOrbis/musl/blob/master/src/unistd/linkat.c
  - https://github.com/OpenOrbis/musl/blob/master/src/unistd/unlinkat.c
  - https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/blob/master/include/orbis/libkernel.h
- يظل `download_file_backend.c` على `__ORBIS__` مقفلًا عبر `attach()=-1` وCallbacks فارغة. لا نستبدل `linkat` بـ`rename` غير مثبت أمانه.

### ما لم يثبت بعد
- `HTTP/2` أو `HTTP/1.0` غير مدعومين في هذا الـParser قصدًا؛ وعلى Adapter القادم التعامل مع 1xx/redirect/status/TLS بشكل حاسم. لا ادعاء أن استخدام SHA-256 يُثبت أصل Retail/FPKG.
- **لا يوجد Native SceHttp/SceSsl Transport فعلي** مع Chain Validation وTimeouts وStreamed body وRange/If-Range واختبار على shadPS4 أو FW14.
- لا يوجد مصدر مخوّل أو PKG download أو Installer حقيقي، ولا Host Resume End-to-End على 206 بعد فقد الاتصال. لا يُعرض أي تقدم وهمي.
- الخطوة المقبلة: `TransportAdapter` قادر على طلب الاستجابة الموثوقة وقراءة body تدريجيًا، مع حد صارم للحجم وإعادة تحقق SHA256، واختبار 206/ETag/انقطاع الاتصال باستخدام Fixture محلي؛ تفعيل FW14 على Hardware آخر مرحلة فقط.

---

## تحديث إضافي 10 نوفمبر 2026 — Host File Backend فعلي وPS4 Fail-closed

**Verdict: MODIFY** — نواة التخزين أصبحت تملك تنفيذًا لعمليات الملفات على Host POSIX فقط؛ ما زال PS4 Backend غير مفعل.

**Code SHA:** `528acf587b3b6c221cd60b9310d0ebabc2cf2ad5`  
**Host CI:** **216/216 PASS** https://github.com/obsp081-coder/ps4-updates/actions/runs/42801480340  
**OpenOrbis Build:** **PASS** https://github.com/obsp081-coder/ps4-updates/actions/runs/42801473677

### ماذا نُفذ
- `ps4/src/download_file_backend.h/.c`: `DsFileBackend` جديد يتلقى **Directory FD من جهة موثوقة**، ولا ينشئ مجلدات أو يقبل Path مصدره HTTP. نسخة Host تتحقق من ملكية المجلد وعدم قابليته للكتابة من الآخرين، وتنسخ الـFD.
- تنشئ `.part` عبر `openat(O_EXCL|O_NOFOLLOW)`، وتقرأ عبر FD جديد مع فحص `device/inode`، وتنفذ `fsync`، وتتحقق من SHA-256 للملف المخزن، ثم تستخدم `linkat` للنشر الحصري مع رفض استبدال الملف النهائي. تنظيف `.part` فقط إذا بقيت هوية الملف مملوكة للجلسة نفسها.
- اختبارات `file_backend_smoke.c` على Host تكتب ملفات فعلية في `mkdtemp` وتغطي النجاح، وجود ملف نهائي، وجود Partial مسبق، Symlink، تبديل Inode، وفساد البايتات. لا تحمل أي ملف لعبة.
- **في بناء PS4: `#ifdef __ORBIS__` يُرجع Backend غير مدعوم (`-1`) وCallbacks فارغة**. اختبار مستقل يجبر هذا المسار على Host ويتحقق من رفض `ds_begin`، حتى لا يحدث تفعيل غير مقصود.
- لا يوجد استعمال إنتاجي للـBackend ولا تشغيل تنزيل في الـPKG؛ البناء فقط يتأكد من التوافق. لم نختبر Runtime على `shadPS4` أو جهاز FW14 في هذا التعديل، لأن الوظيفة القابلة للتجربة حاليا Host فقط.

### لماذا لم نفعّل PS4 Backend
- المصدر الأولي `OpenOrbis/musl` يبين أن `linkat` على PS4 قد يعيد `ENOSYS` دون `PS4_LIBKERNEL_SYS`، و`unlinkat` على PS4 يعيد `ENOSYS` في التنفيذ المتاح؛ `fsync` المعتاد معرّف خارج `PS4`. هذه أدلة **Code inspection** وليست تجربة FW14.
  - https://github.com/OpenOrbis/musl/blob/master/src/unistd/linkat.c
  - https://github.com/OpenOrbis/musl/blob/master/src/unistd/unlinkat.c
  - https://github.com/OpenOrbis/musl/blob/master/src/unistd/fsync.c
- تتضمن `libkernel.h` تصريحات `sceKernelOpen` و`sceKernelFsync` و`sceKernelRename` و`sceKernelUnlink`، لكن مسار نشر مضمون **No-overwrite** لم يثبت؛ `Rename` وحدها لا تعوّض `linkat`. لا نصطنع واجهات PS4 أو نفترض أن مجرد Compile يثبت دعمها.
  - https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/blob/master/include/orbis/libkernel.h
- حتى Host backend ليس ضمانًا ضد جهات خبيثة تملك حق الكتابة المتزامنة في مجلد التطبيق؛ يفترض مجلدًا خاصًا وعملية نقل واحدة، مع فحوص هوية ملف تخفف السباقات ولا تلغيها. `fsync` للملفات والدليل والتعافي بعد سقوط الكهرباء/Runtime Crash ما تزال بحاجة اختبارات أوسع قبل الاعتماد.

### ما بقي
1. إثبات **PS4-safe exclusive publish** وOwnership/Directory handling بدون الكتابة فوق ملفات مسبقة أو الاعتماد على Syscalls غير مدعومة.
2. ربط `DsPort` بـNative Storage Backend في نطاق مسار بيانات التطبيق المملوك فقط وبعد التحقق من الصلاحيات؛ لا اقتراب من `/user/app` أو ملفات الألعاب والحفظ.
3. HTTPS/TLS Certificate verification، مصدر مخوّل وHash مستقل، ثم نقل Fixture مصرح به مع Cancel/Retry/Resume موثوق.
4. اختبار `shadPS4` فقط عندما يصبح لدينا مسار Native قابل للتشغيل؛ **اختبار PS4 الحقيقي في المرحلة النهائية** بناءً على طلب المستخدم.

**حالة الأمان:** `download_enabled=false` و`install_enabled=false` باقية في التطبيق. لا Release جديد ولا ادعاء دعم FW14 حقيقي.

---

## تحديث لاحق 10 نوفمبر 2026 — SHA-256 Native فعلي على الملفات المخزنة

**Verdict: MODIFY — نجاح Native C SHA-256 + Host + OpenOrbis؛ PS4 Hardware وDownloader الفعلي ما زالا غير مثبتين.**

- أضفنا `ps4/src/download_sha256.h/.c` بمحرك SHA-256 مبني على قواعد `FIPS 180-4`، قادر على العمل تدريجيًا بكتل صغيرة، وبحد منطقي لعدد البايتات وإعادة استخدام السياق بعد Final. لا يعتمد على الإنترنت أو مكتبات Crypto إضافية.
- `ds_sha256_verify_stored()` يعيد فتح ملف `.part` عبر `DsSha256Reader` من Backend مقيّد، ويقرأ كل المحتوى ليتحقق من **حجم مطابق تمامًا** و**بصمة SHA-256 مطابقة** مع إغلاق ناجح. لا يقرأ البصمة من `Content-Length` أو يثق بمحتوى غير موثق. `ds_finalize()` يمرّر `expected_size` إلى `verify_sha256` قبل `publish_exclusive`.
- استبدلنا فحص Fixture القديم الذي يعتمد على Token تجريبي، بتجزئة **الملف المخزن فعليًا** في `tests/ui_mocks/storage_smoke.c`. مرجع Digest مستقل محسوب باستخدام Python `hashlib` لملف اختبار حجمه 41 بايت.
- رفضت اختبارات Host حالة تغيير بايت واحد في الملف، والقطع، وإضافة بايت زائد، وتقديم Digest خاطئة، وفشل القراءة، ومحاولات نشر ملف غير معتمد.
- اختبارات خوارزمية SHA-256 تشمل Empty/abc/55/56/64/65/256 Byte ومليون بايت، وتعدد دفعات `update()` وحماية Overflow، باستخدام Test Vectors مستقلة. توجد في `tests/ui_mocks/sha256_smoke.c` و`tests/test_download_sha256.py`.
- **Code SHA:** `0d3392fad23499a4b25c72b40bf6e269eaa41d26`.
- **Host CI:** **214/214 PASS** — https://github.com/obsp081-coder/ps4-updates/actions/runs/42794816996
- **OpenOrbis Package Build:** **PASS** — https://github.com/obsp081-coder/ps4-updates/actions/runs/42794809447
- كشف بناء OpenOrbis الأول خطأ في Makefile (`clang: no input files`)؛ أُصلح بإضافة مصدر `$<` للأمر، ثم أُعيد CI حتى النجاح. لا يوجد Release نهائي جديد.

**حدود الدليل:** يعمل حساب SHA-256 فعليًا على Host، ونجح Compile/Link على OpenOrbis؛ **لم تُثبت سرعة التجزئة أو صلاحيات القراءة/الكتابة على FW14**. Backend التخزين الوحيد الفعلي في الاختبارات هو POSIX Host، وليس PS4. `SHA-256` يثبت التطابق مع بصمة مرجعية **إذا كانت مرجعية مستقلة ومصرحًا بها**، ولا يثبت وحده الحقوق أو توقيع Retail/FPKG أو التوافق. التطبيق الفعلي يبقى **Read Only**، و`Download/Install` معطّلان.

**التالي:** تنفيذ `PS4 Storage Port` مع مراجعة APIs ومسارات التطبيق وقيود `openat/O_EXCL/fsync` قبل أي Runtime تفعيل، ثم HTTPS/TLS Transport موثوق مع فحص الشهادات؛ تنزيل Test Fixture صغير فقط على محاكي عند جدواه، وPS4 Hardware Acceptance في النهاية.

---

## تحديث 10 نوفمبر 2026 — StorageAdapter Native بلا صلاحيات كتابة على PS4

**Verdict: MODIFY — نجح كود إدارة ملفات النقل على Host، ولم يُفعّل على PS4.**

- **Code**: `ps4/src/download_storage.h/.c` يعرّف `DsSession` و`DsPort`، وهي طبقة C99 محمولة للتمييز بين `.part` والملف النهائي. تُبنى مع OpenOrbis لاختبار التوافق فقط؛ لا يوجد `PS4 filesystem backend` في التطبيق بعد.
- تقبل فقط `Content ID` و`Version` صالحين لتوليد اسم ملف آمن دون Slash أو Path Traversal. سقف كتلة الكتابة الواحدة **64 KiB**. كتابة حجم زائد أو نشر ملف غير مكتمل مرفوضة.
- تعتمد على **Backend موثوق منفصل** لينفّذ `create_exclusive` و`write_all` و`flush` و`close` والتحقق من SHA-256 الفعلي للملف المخزن و`publish_exclusive` و`unlink_part`. كود Native لا يحتوي حتى الآن Backend PS4 حقيقيًا ولا خوارزمية SHA-256 محمولة.
- لا يعتبر وصول آخر بايت نجاحًا؛ المسار الإجباري: **Full size → fsync/close → verify stored file → publish without overwrite → cleanup owned partial**. فشل أي خطوة يُبقي الحالة غير معتمدة ويحفظ الملف النهائي السابق دون تغييره.
- لا يحذف `.part` أقدم منه عند فشل `create_exclusive`. الفشل في تنظيف ملفه الجزئي بعد نشر الملف النهائي يترك حالة `PUBLISHED + cleanup_pending` قابلة لتنظيف الجزئي فقط، دون حذف النهائي.
- **Host backend في `tests/ui_mocks/storage_smoke.c` تجريبي POSIX**: يكتب بايتات حقيقية في مجلد مؤقت، ويرفض الاستبدال، ويختبر حالات فشل الكتابة و`fsync` و`publish` و`cleanup`، مع اختبار عدّ بايتات `Download Queue`. **مهم:** دالة `verify_sha256` في هذا الاختبار تعتمد مقارنة محتوى Fixture المعروف وToken تجريبي، **وليست تطبيقًا حقيقيًا لـSHA-256**. اختبار SHA-256 الحقيقي السابق منفصل في Python Host harness ولا يثبت Native PS4.
- **Host CI على Code SHA `943d33ebb4095fa451c0346f9c9d362260bf9257`: 213/213 PASS** — https://github.com/obsp081-coder/ps4-updates/actions/runs/42792501024
- **OpenOrbis CI على SHA نفسه: PASS (Compile + PKG header verification)** — https://github.com/obsp081-coder/ps4-updates/actions/runs/42792500963
- **لا Release رسمي جديد، ولا تم تفعيل Download/Install**. حزمة Preview تبقى Read Only. دعم `open/O_EXCL/fsync/link` على FW14، مسار تخزين التطبيق والصلاحيات، وBackend SHA-256 الحقيقي **Unverified**.

**التالي:** Native File Backend آمن خاص بمجلد التطبيق بعد فحص دعم APIs والحدود، ومحقق SHA-256 فعلي، ثم HTTP/TLS Adapter موثوق وربط Queue ببايتات النقل الحقيقية. تعاد اختبارات Host وshadPS4 عند الحاجة؛ **اختبارات PS4 الحقيقي آخر مرحلة بناءً على طلب المستخدم**، وتظل أزرار التنزيل/التثبيت غير مفعلة قبلها.

---

## تحديث إضافي — Native HTTP Response Safety Policy

**تغيير Code:** `3970da3e9183fe4698d652ba806e7f957d7718a0` — أضيفت الوحدة `ps4/src/download_http_policy.h/.c` إلى Native PS4 Build و`tests/ui_mocks/http_policy_smoke.c` و`tests/test_download_http_policy.py`.

- يفحص **Metadata من Parser موثوق لاحقًا** ولا ينشئ HTTP/TLS Session. لا يقبل `tls_authenticated` المفقود، والتطبيق ليس لديه TLS adapter حقيقي حتى الآن.
- يمنع Response غير `200` للتنزيل من الصفر، ويشترط `206` مع Range/If-Range وStrong ETag مطابق للاستكمال.
- يتحقق من `Content-Range` الدقيق من Offset إلى نهاية الحجم الموثوق ومن `Content-Length` المتبقي. يرفض `Transfer-Encoding`، Content Encoding، التوجيهات، وETag الضعيف أو المحقون.
- الاختبارات Native C اصطناعية، لا تثبت HTTPS مصدّقًا أو تحميلًا على الجهاز.
- **الحالة:** تنفيذ وبناء Host المحلي للدالة تم، ونتيجة CI لآخر Code SHA يجب التحقق منها قبل اعتماد نجاح OpenOrbis. إنتاج التطبيق يبقى `Read Only`.

**المتبقي:** Parser حقيقي لترويسات HTTP، وتحقق سلسلة شهادات TLS، ومستودع مصدر مخوّل، وNative StorageAdapter وHash/Partial-state recovery؛ لا يوجد Resume حقيقي أو تنزيل PKG على PS4.

---

## تحديث 10 نوفمبر 2026 — نقل HTTP محلي فعلي للتحقق على Host

**الحالة:** تم إضافة `tests/host_fixture_transfer.py` و`tests/test_host_fixture_transfer.py` كـ**اختبار تكامل على الكمبيوتر فقط**. هذا ليس كود Downloader يعمل على PS4، ولم يُفعّل تنزيل تحديثات ألعاب أو تثبيت PKG.

- ينشئ الاختبار `ThreadingHTTPServer` محليًا على `127.0.0.1` ويوفر محتوى اصطناعيًا من صنع الاختبار.
- العميل يتصل فقط بـ`http://127.0.0.1`، ويرفض المضيف الخارجي وإعادة التوجيه، ولا يستخدم Proxy.
- يرفض `Content-Length` المفقود أو المختلف، `Transfer-Encoding` غير المدعوم، الضغط، رد 404، الاستجابة المقطوعة، وملفات مختلفة البصمة.
- يفحص الحد الأقصى للحجم (8 MiB) والمساحة، ويسجل البايتات المنقولة فعليًا في SHA-256؛ ولا يعتمد حجم الملف أو البصمة على ترويسات المصدر وحدها.
- ينشئ ملف `.part` بشكل حصري، ويفحص SHA-256 قبل نشر ملف نهائي بدون الكتابة فوق ملف موجود. في الفشل يلغي **فقط** الملف الجزئي الذي أنشأه هو.
- كشف الاختبار خطأً كان سيمحو `.part` سابقًا في حالة فشل فتحه؛ تم إصلاحه باشتراط ملكية الملف (`owns_partial`).
- **نتيجة محلية:** `9/9 Host integration tests PASS`. لا تدّعِ اختبارها على محاكي أو جهاز PS4.
- **CI GitHub:** راجع https://github.com/obsp081-coder/ps4-updates/actions?query=branch%3Afix%2Fscanner-cid-and-inventory-capacity؛ يجب التحقق من نتيجة آخر SHA قبل وصف CI بالناجح.

**القيود:** هذا النموذج *ليس HTTPS* لأنه مقصور على localhost للاختبار. لا يوجد Native PS4 `SceHttp` downloader أو `SHA-256` مربوط بـQueue؛ ولا يدعم هذا النموذج Resume بعد إعادة التشغيل. مرحلة Real Transport + `StorageAdapter` Native ما زالت مفتوحة، وFW14 Hardware مؤجل للنهاية.

---

# المرحلة الخامسة — Downloader | حالة التطوير

**تاريخ المراجعة:** 10 نوفمبر 2026
**القرار:** MODIFY — تنفيذ Offline-first مع تأجيل اختبار FW14/GoldHEN الحقيقي للنهاية.
**Code SHA لهذا الإنجاز:** `0d2063752703c87ee842c9ff745057a85a8adca7`.
**تنبيه:** وجود كود Queue لا يساوي تنزيلًا حقيقيًا. لا تنزيل أو تثبيت في Production.

## ما أنجز برمجيًا
- `ps4/src/download_queue.h` و`download_queue.c` يعرّفان `DlqQueue` بـ16 مهمة كحد أقصى، مع هوية `CUSA/Content ID/Version` صحيحة وبلا تكرار.
- State Machine: `QUEUED → ACTIVE → PAUSED/VERIFYING → COMPLETED/FAILED/CANCELLED`.
- تنزيل واحد في الوقت ذاته؛ `VERIFYING` يمنع بدء مهمة أخرى حتى اكتمالها أو فشلها.
- `dlq_space_check` يحسب `download bytes + install workspace + safety reserve`، ويرفض Overflow، ويعيد مقدار العجز. **قيمة Workspace يجب تأكيدها من مصدر موثوق؛ الصفر ليس إثباتًا لكفاية مساحة التثبيت.**
- يمنع تسجيل Bytes تتجاوز حجم الملف المتوقع؛ لا ينقل بيانات ولا يصطنع Progress.
- `Resume` يظل مرفوضًا ما لم تُقدَّم إثباتات حقيقية لثبات المصدر وسلامة الملف الجزئي. التحقق الفعلي من ETag/Range/Hash **غير منفذ**.
- بعد اكتمال عدد البايتات يدخل الملف `VERIFYING`. مجرد اكتمال البايتات **ليس** اكتمالًا موثوقًا. SHA-256 الفعلي غير موصول بعد؛ نتيجة الفحص حاليا مُدخلة من طبقة مستقبلية غير موجودة.
- عند فشل Integrity تبقى المهمة `FAILED`، و`Retry` يبدأ من الصفر مع عداد محاولات؛ لا إعادة استخدام جزئي غير موثوق.
- تمت إضافة `download_queue.o` إلى بناء PS4 لاختبار Compile/ABI فقط؛ لا توجد أي وظيفة Production تستخدم الوحدة لتشغيل نقل، ولا بيانات مهام وهمية في الواجهة.

## الاختبارات
- `tests/ui_mocks/download_queue_smoke.c` و`tests/test_download_queue.py`: فحص نقص المساحة، Overflow، التكرار والسعة القصوى، one-active-job، حدود حجم البايتات، Pause/Resume، رفض Resume غير الموثق، تحقق Integrity الفاشل والناجح **الاصطناعي**، Retry وCancel.
- **Host CI على SHA أعلاه: 202/202 PASS** في https://github.com/obsp081-coder/ps4-updates/actions/runs/42787040073
- **OpenOrbis Build:** راجع https://github.com/obsp081-coder/ps4-updates/actions/runs/42787029684 — لا تعتمد نجاحه قبل وصوله للحالة النهائية.
- **shadPS4:** غير مطلوب لهذه الخطوة لأنها Pure C غير مرتبطة بالواجهة أو APIs؛ يُستخدم لاحقًا لاختبار UI وNetwork lifecycle إن دعمها المحاكي. لا يعوّض GoldHEN FW14.
- **PS4 Hardware:** مؤجل بطلب المستخدم إلى نهاية المراحل، وليس مثبت النجاح.

## القادم (الترتيب الإجباري)
1. `TransportAdapter` بنقل HTTPS حقيقي **لمحتوى اختبار صغير مصرح به فقط**. البحث الأولي وجد مثال OpenOrbis `samples/net_http` يستخدم `SceNet/SceSsl/SceHttp`، لكن الأداء والتحقق من TLS على FW14 غير مؤكدين.
2. `StorageAdapter` بملفات .part وعملية حفظ ذرية وحدود RAM ومساحة حقيقية. لا تلمس ألعاب المستخدم أو ملفات الحفظ.
3. تحقق SHA-256 باستخدام تنفيذ موثوق، `Content-Length`، و`Range/ETag` للـResume مع رفض التغيرات.
4. ربط `Download Queue` بالواجهة حصريًا من مهمة نقل حقيقية، واختبار Cancel/Retry/Resume/Offline/Low storage بملف مصرح به على Host، ثم shadPS4 إن كان مفيدًا.
5. لا تفعّل تنزيل ملفات تحديثات الألعاب إلا بعد مصدر مخوّل وفحص التوافق والحقوق؛ ولا تفعّل Installer قبل Hardware Proof.

**بوابات PS4 المؤجلة:** نقل فعلي بإذن المصدر، صلاحيات التخزين، حجب تحديث النظام، حجم الملفات، تعامل الإلغاء والانقطاع، ثم اختبار التثبيت في المرحلة السادسة.

