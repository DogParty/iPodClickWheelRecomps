# Ported from the Mini Golf recomp

Written by `tools/port-from-minigolf.py`; do not edit by hand. Every file here entered
this tree as a copy of the file named in the second column, with the name rewrites that
script applies. `source` is the SHA-256 of the Mini Golf original at the moment of the
port; `ported` is the SHA-256 of this tree's copy immediately after it — so a file whose
current hash differs from `ported` has been edited here, and one whose Mini Golf
original differs from `source` has moved on there. A row whose file is absent here was
ported and then deliberately removed; the reason is in the script's `PORTED` list.

`python3 tools/port-from-minigolf.py --check` reports both.

Source tree: `../Mini Golf`

| file | source SHA-256 | ported SHA-256 |
|---|---|---|
| `tools/emit.py` | `02d8352e15f1f097…` | `02d8352e15f1f097…` |
| `tools/funcs.py` | `2ee0369146170998…` | `2ee0369146170998…` |
| `tools/progress.py` | `a8c921ac59da9c29…` | `a8c921ac59da9c29…` |
| `tools/manifest.py` | `f7c656b2667872d3…` | `84d6590129d8f949…` |
| `tools/ppm2png.py` | `3e066b26c614c9e9…` | `3e066b26c614c9e9…` |
| `src/gamedata/install.cpp` | `e8d56947610a74fd…` | `ecd566a5598359f3…` |
| `src/gamedata/install.h` | `5849f063407ab068…` | `ef31c336b5f78eca…` |
| `src/gamedata/manifest.h` | `4561946d9260b69e…` | `9c33a62bb10374bf…` |
| `src/gamedata/zip.cpp` | `19bef9c63427f9ff…` | `06f80bc8379ce3d6…` |
| `src/gamedata/zip.h` | `02a47f57ca4880f1…` | `a121bc12b5606436…` |
| `src/runtime/arm_runtime.json` | `84bb7ab8e1c52940…` | `84bb7ab8e1c52940…` |
| `src/runtime/cpu.h` | `053c787babc3d45c…` | `19a6ec30052c3cdd…` |
| `src/runtime/eapp_image.cpp` | `a28b517df70c08ee…` | `3d3f47d0762b887f…` |
| `src/runtime/eapp_image.h` | `11a3c0211079548c…` | `7e146b12851dd9bd…` |
| `src/runtime/main.cpp` | `9aec6e1374ec2cf7…` | `13555d3c315f26e8…` |
| `src/runtime/memory.cpp` | `2e75eb281d5af005…` | `eb962d4c542d2efa…` |
| `src/runtime/memory.h` | `d063a77794983174…` | `d577eda64166fd84…` |
| `src/runtime/runtime.cpp` | `021a3ae6b8c88b47…` | `9047895befad3663…` |
| `src/runtime/runtime.h` | `143d8d3d20dda00a…` | `ca113bd77f1abb96…` |
| `src/framework/audio.h` | `476988e2c7b71bc4…` | `43df1ecd8ab1b7ec…` |
| `src/framework/controls.h` | `fef994fecdca66fb…` | `5bb34049a5b6912c…` |
| `src/framework/device.h` | `c0c6350415926c38…` | `f04a4f606bfa1be5…` |
| `src/framework/graphics.h` | `275afc6674c8935a…` | `75af1a6a29d6cb5b…` |
| `src/framework/storage.h` | `e072f5fb23844547…` | `1c4d41678a1e2d6d…` |
| `src/framework/types.h` | `36f293de0c109bcd…` | `87a0b748161adaad…` |
| `src/libeapp/include/ipod_eapp.h` | `70287cfdebd9d2c6…` | `7f0553f68f964294…` |
| `src/libeapp/arm_abi.cpp` | `43fa19a10f64b4dd…` | `42a23403ff1c3389…` |
| `src/libeapp/async_file.cpp` | `127dd562056a3b59…` | `19a2a1cf04bc2460…` |
| `src/libeapp/audio.cpp` | `ce5f1965dfabb6bc…` | `0bf051df724c39bd…` |
| `src/libeapp/framework_call.cpp` | `98c24a6e5d27045f…` | `3e7f2cf1295a5fa9…` |
| `src/libeapp/gles.cpp` | `f3ae345279aae981…` | `2ce146c2e6f2d69e…` |
| `src/libeapp/gles.h` | `d242470ce64932d5…` | `3009aff2140c36fc…` |
| `src/libeapp/heap.cpp` | `50d604bed2e56a6c…` | `be36890a659c03d0…` |
| `src/libeapp/heap.h` | `eeed37645a1df85e…` | `66d1fa2fdf1d7ea2…` |
| `src/libeapp/host_state.cpp` | `b2e807ec43292e26…` | `69d39c64b4c95419…` |
| `src/libeapp/host_state.h` | `c3c6ca7c9bba5007…` | `5492d66cb35a32ed…` |
| `src/libeapp/imports.json` | `5ad61b75c8ac3c39…` | `5ad61b75c8ac3c39…` |
| `src/libeapp/input.cpp` | `cc79464a12eaa517…` | `6e497221365615a5…` |
| `src/libeapp/misc.cpp` | `e7712f381b8255b3…` | `45f39852a23801bb…` |
| `src/platform/input_bindings.cpp` | `cc8382bfa5455d23…` | `bec1142c11ccc7c4…` |
| `src/platform/input_bindings.h` | `46bc010614851ca6…` | `a1a3846a77cfcee5…` |
| `src/platform/paths.cpp` | `93c346637aaa8097…` | `dea39ca559fce97e…` |
| `src/platform/paths.h` | `d7730195dfc7a672…` | `6bb822371ca00bb2…` |
| `src/platform/platform.h` | `0e816ee09c146543…` | `3f9d8a79de477236…` |
| `src/platform/save_store.cpp` | `885468539e47ffa4…` | `d19a7104e853a796…` |
| `src/platform/save_store.h` | `ac0ed53798b06afc…` | `4d09db2d9ecbc005…` |
| `src/platform/settings.cpp` | `c69431809a990f8c…` | `5ff70e2098468340…` |
| `src/platform/settings.h` | `971c3da34d5e13fb…` | `348cf51bed2224ec…` |
| `src/platform/text_entry.cpp` | `0dcb0e8975e8fa37…` | `bff503e91119eb87…` |
| `src/platform/text_entry.h` | `1910fc49279bc0d0…` | `407da5f5fafe777b…` |
| `src/platform/wav.cpp` | `b29de737325c0933…` | `faead1f80c77939b…` |
| `src/platform/wav.h` | `de104fddc384660d…` | `3b31e5a3517ccc50…` |
| `src/platform/null/null_platform.cpp` | `d9db622766066b8b…` | `495150e39b0a05fe…` |
| `src/platform/sdl3/sdl3_platform.cpp` | `197f1a00e49ba018…` | `3cf51a895ac5e810…` |
| `src/platform/sdl3/macos_settings.h` | `f383878faf604fbf…` | `84fbd391c76b9434…` |
| `src/platform/sdl3/macos_settings.mm` | `b0c35f719abce0c9…` | `f362c4de2ef825d9…` |
| `src/platform/sdl3/macos_settings_stub.cpp` | `42bd8958346eb876…` | `66d03e49f09573ef…` |
| `tests/diff.py` | `4a178b48b6083a33…` | `4a178b48b6083a33…` |
| `tests/diff.sh` | `93fd55dd6bd79236…` | `eba2037bf8d5aa61…` |
| `tests/unit/cpu_test.cpp` | `8e9c99b1f77c0899…` | `e4ebca8b1084e65e…` |
| `tests/unit/input_bindings_test.cpp` | `c35412fd8a2681b3…` | `385cd0d457726c70…` |
| `tests/unit/save_store_test.cpp` | `1949f263e1969259…` | `f323110b7b41a63a…` |
| `tests/unit/settings_test.cpp` | `815f4b1ab54ac950…` | `5e4801838e95f75a…` |
| `tests/unit/text_entry_test.cpp` | `bcfa32bda387386e…` | `256272cd068a76e6…` |
| `tests/unit/wav_test.cpp` | `2a15b7ea184c03fe…` | `d862d6b503dc904c…` |
| `.clang-format` | `e395beaa072bb3a7…` | `e395beaa072bb3a7…` |
| `.gitignore` | `63be401b60760b75…` | `63be401b60760b75…` |
| `CMakeLists.txt` | `e790502de50f2ec5…` | `0ca930497a6dd4f2…` |
| `pyproject.toml` | `4ac3703cc28f121f…` | `4ac3703cc28f121f…` |
| `src/runtime/arm_runtime.cpp` | `7e67c4e474fe0239…` | `44f0c136b6160b3d…` |
| `src/runtime/arm_runtime.h` | `3a9746475421d4e3…` | `5864cf202ce58884…` |

<!-- full hashes, for the --check command
tools/emit.py 02d8352e15f1f09780fb135b3316c98d54efa37e8f59ba9245187d9a4ba0c562 02d8352e15f1f09780fb135b3316c98d54efa37e8f59ba9245187d9a4ba0c562
tools/funcs.py 2ee03691461709984ea0f76bba0349f650ef19400f7adbc0efda1163c642bebb 2ee03691461709984ea0f76bba0349f650ef19400f7adbc0efda1163c642bebb
tools/progress.py a8c921ac59da9c293fe7cac993b4ca09fd5f281fc27e75a35172693cb40a9205 a8c921ac59da9c293fe7cac993b4ca09fd5f281fc27e75a35172693cb40a9205
tools/manifest.py f7c656b2667872d399490b79265a400e54484b8280218da2a5617a1f68131ac5 84d6590129d8f949e502fecc033fee020c4985ddd5740d103c0940c10fc7f523
tools/ppm2png.py 3e066b26c614c9e9122d39f742d075444ace425f99a2317f37e2d9b5e769bfdf 3e066b26c614c9e9122d39f742d075444ace425f99a2317f37e2d9b5e769bfdf
src/gamedata/install.cpp e8d56947610a74fda9eccce2bd029eb61471d32a8c0f9741728c517462c61290 ecd566a5598359f3fbc56d378c53a2e4fb4dcc025b480941becc8aed35c8da09
src/gamedata/install.h 5849f063407ab0684b47c2bb08c538e6b4f4717a0ce9befa19558611f211563b ef31c336b5f78eca11f49dedaa248eed8bd1d50e72408b6c796968c01a98376c
src/gamedata/manifest.h 4561946d9260b69ed3a08ab17e87157521a88e836b6abbbf90330583f5c36c5f 9c33a62bb10374bfdf444d534d8478916c218563721973ad341a83b257e61842
src/gamedata/zip.cpp 19bef9c63427f9ffbce5e2473e2ef41d9123b464487dacc25e9d3b5d65bcd970 06f80bc8379ce3d6d1cab38ab42739423b90a7f333b8f30b99506b92965981ac
src/gamedata/zip.h 02a47f57ca4880f1745c439a547b6cc582f80f28642ecea75be7f2b931700094 a121bc12b56064362960f5ce646d41b705e0344ce41ac4113d7bf743673c150c
src/runtime/arm_runtime.json 84bb7ab8e1c52940d19e815129b773fbc126710da1122529a962cd59606d1aaa 84bb7ab8e1c52940d19e815129b773fbc126710da1122529a962cd59606d1aaa
src/runtime/cpu.h 053c787babc3d45c2aa2142a2895929fb62511268f506cd1e2ad140eede5fe6c 19a6ec30052c3cdde0c2dc0a5e9ab9713cf6db75ccf0d69b19684887bda5aa30
src/runtime/eapp_image.cpp a28b517df70c08ee41f4cb8ce591688806ecceea962b892e2c020ccfdaf9f499 3d3f47d0762b887f0e1637dfefda697d9e4dd4d37b95b5f5562956fdf3fac219
src/runtime/eapp_image.h 11a3c0211079548c2a0ba5e65369540ff57ea20f790af5ea61954a7b20122d05 7e146b12851dd9bdc2bfdf75b6252db26dc1c280fbd56641e815083d6dd42b50
src/runtime/main.cpp 9aec6e1374ec2cf79560711d83c3a688570f0d06c3177fb629467fad0ba9b4f3 13555d3c315f26e855c9d3ffafc5d4666c7161c9f60361a826a5f68f67ed5dbc
src/runtime/memory.cpp 2e75eb281d5af0051a14658f448d9177dfad0f357d9ae4e0215a99e5d2e91d47 eb962d4c542d2efa47caaffb9c2df5d12a3a0496fa1dcb8bf23ba0122bdf89ad
src/runtime/memory.h d063a77794983174abc8de4b3e764d1cb08e0a953674de608715747846ec55d3 d577eda64166fd84abfca67ee58b31e4779601ffd3d6db0dd66be5c0cee92d5a
src/runtime/runtime.cpp 021a3ae6b8c88b47b997279210a5f775a80abe65082812ec97c151dd3c4585af 9047895befad3663221057cada0d6d773a6f0f20986feee179855d80a6aaf186
src/runtime/runtime.h 143d8d3d20dda00a8bd22c8c340e781e9e4b2c419d323827437ade6e68eef2a8 ca113bd77f1abb96174a6593028d995cec3c383c0d9638b6ce92adc2f3cff42a
src/framework/audio.h 476988e2c7b71bc48aa39028a5aaf7388391e8b814bc9389862ff195f0541fc5 43df1ecd8ab1b7ec47dff7b457e37295b05951f968075867e88b074f6184b126
src/framework/controls.h fef994fecdca66fb4a222316a0cc29fffd89aac83a6e59234130c0623f983c36 5bb34049a5b6912c593fe06d8e8693386d3b83f510a3b212dbaec28c1f643a8f
src/framework/device.h c0c6350415926c38b960326111f24314d6e81bca7a3e5be47858501093dfd600 f04a4f606bfa1be528352615a745d6c3c523165f74e3d5480da3e9c6545b6399
src/framework/graphics.h 275afc6674c8935a60b151bbad466114e632006931604e410cc4ebcad8727fd0 75af1a6a29d6cb5b67a042c083d3da1d82d4f3a25ca31a760b4e15c494903445
src/framework/storage.h e072f5fb23844547792749e5f7a965639bd4ded16577a299d31917c6d3094abe 1c4d41678a1e2d6d49e12c478eca1c34324f72ab081f974f758d378e82a14e62
src/framework/types.h 36f293de0c109bcd049aef2ba84e975e6f66f19512015724f31b84a8db7b1f9e 87a0b748161adaad4068c91d375f64eaf41f23a0b8aa78792777e1af289bed52
src/libeapp/include/ipod_eapp.h 70287cfdebd9d2c65fa2dc4f41d3a846768124f8bf0bde230eae6ba774e72a8b 7f0553f68f964294cb202609672168859067384ed067fa0b22b33dedfe593d5d
src/libeapp/arm_abi.cpp 43fa19a10f64b4dd1e558ef75899c54279a9a09d567014b1f052509a5bda63f0 42a23403ff1c33897d50ddfe82f21396e71e1769e07d0a119935bc885f61cc94
src/libeapp/async_file.cpp 127dd562056a3b592370039d4a1882c744c0c72b5875c3efe9733ef14c46ee1a 19a2a1cf04bc24601a5368945ab45101a4bb52977170d9b142bdc5009913ecc7
src/libeapp/audio.cpp ce5f1965dfabb6bcf15ac8bab3d4fb699a013453aa3e2fa5fd0ca1f06a12f3cb 0bf051df724c39bded6dcb717f87cb958b71b041862b5c58ecf15fdd59be2c70
src/libeapp/framework_call.cpp 98c24a6e5d27045f3329725c3980e3a0c82d8226c7629527c1d99b91091f0538 3e7f2cf1295a5fa92c38dcd232db656d69d20537c006a35fd76492699fa2a5de
src/libeapp/gles.cpp f3ae345279aae98113ae3651ffe23c596b7def5732ce4753f24f78e721c3ca75 2ce146c2e6f2d69e7179066386b84d1923a413ab784c395e886a173f5fee7319
src/libeapp/gles.h d242470ce64932d5556e8f783fb21ed9a3c11c379778076e4de7bd0fb046250a 3009aff2140c36fc1a48ef395a6d469a5c4faf5da4b0032eb94aec23e4fd099c
src/libeapp/heap.cpp 50d604bed2e56a6c747727137ce69eff82066d7e558f4342112b1644aaec2b71 be36890a659c03d0dfad939ece5d9439528e37c0fcca42965a2ae3681d8bf444
src/libeapp/heap.h eeed37645a1df85e33da862e9ca5243b3167a82031e685f00fe229f5d5563316 66d1fa2fdf1d7ea2ab0b3272a0067eae67045fb8829a289d943083bf7e90357c
src/libeapp/host_state.cpp b2e807ec43292e2671100201747e01d86475829efe47753c346c54899e2ac1d0 69d39c64b4c954190fa0c111dc2671d0b44cb90b7737ac7fd8fc3e6b9fca94fb
src/libeapp/host_state.h c3c6ca7c9bba5007386e39e31683f2883efd4830c283b099d5bd32bd37e87bd7 5492d66cb35a32ed48499684d985dc98d549a7aad1742cb5be947f6cd0090a7e
src/libeapp/imports.json 5ad61b75c8ac3c39d1575d8fa3675325c5ddef041bc3111e9183f50f601118bb 5ad61b75c8ac3c39d1575d8fa3675325c5ddef041bc3111e9183f50f601118bb
src/libeapp/input.cpp cc79464a12eaa51785d73f15fcda7dba225aa729b95714d783bd92cb3f96087d 6e497221365615a5eddbc6685c772eca7667fcfd71919b6e3cce34941bda3ea0
src/libeapp/misc.cpp e7712f381b8255b3613f0d3449748fd4673ac3c95d848bf3a900b2f4b1d46f58 45f39852a23801bbd71aa074203378cd1a62ebd674a044a7c9bdb768738566bb
src/platform/input_bindings.cpp cc8382bfa5455d235b143ba21eed78e33c37e119d97e407b295c4c25a52f2578 bec1142c11ccc7c4cf09a56c7093906e7ab9f12c64f2af874776fe65c44db184
src/platform/input_bindings.h 46bc010614851ca61b6952f919cc1858e06c435bd0e085d72a80276842640a0f a1a3846a77cfcee56480ad319df10e75f5ff7d65124488aa6578e894a45b8f3a
src/platform/paths.cpp 93c346637aaa809700b355bcd5b1543485e204441f0917097c05012704161eca dea39ca559fce97ef6b175908560b0ea3d37f60729eab56d235bc566d256ca00
src/platform/paths.h d7730195dfc7a6723af376ac9af65f4555ccff41539603d0a07d7e37967b069d 6bb822371ca00bb2330c9cc681ab0411b984b1db68f8c1b4fada94b233525d9e
src/platform/platform.h 0e816ee09c1465433aeda88fa9ea38734762624d1f47418e8646a460632a116a 3f9d8a79de477236a7a496c522e7a90be4fe3ca124554df0446bc52897b505ba
src/platform/save_store.cpp 885468539e47ffa4d08846a1e3ff21ff1b4e46b5957461d957cedebb1ad72450 d19a7104e853a79640365841a9500d2c1307f112d038602e2989596cc0c62f3b
src/platform/save_store.h ac0ed53798b06afcc37053b2e6028e6ca84094df3abe3afacb65e6e078c2c917 4d09db2d9ecbc005141ddeecfce00f6c5b27a23422e2677c7b1c2c40fb416b8b
src/platform/settings.cpp c69431809a990f8c254ca2a7b401a335a411e9e49f522bd49e4b7c5dc083b298 5ff70e2098468340795d85e67b4175fba088b3521c412d778cb5b485871e367b
src/platform/settings.h 971c3da34d5e13fb86b086aeca16f67fdf2cf33dd6be37f12d4110564ca2fc21 348cf51bed2224ec665be352f190f920daf3f144d9b54d6c51da65f834e82659
src/platform/text_entry.cpp 0dcb0e8975e8fa37c238ff693c0424ffbca7246f013ecc61983e4dd3cd1c65b8 bff503e91119eb87b5c17bce3cfdca370907936e66267ebbeba558960222f05b
src/platform/text_entry.h 1910fc49279bc0d0efe08f5a204a19bced44306a2127e833863ede515104b16e 407da5f5fafe777bee581d1d77e618917421862a1a5dd88fda2c46cf46ca18b4
src/platform/wav.cpp b29de737325c093303c85036296fff1ede4acb71e7e546215f390ba785aa1a96 faead1f80c77939bee9051b4978221c59b3c9e90e399f8dc195a945cd02d73a7
src/platform/wav.h de104fddc384660d8a17f1bf0c63fb703c3376e08983e598020f765976625237 3b31e5a3517ccc50b1534de301b4b5094b15d235a7d5ddc632d43bdb6dd4e5ca
src/platform/null/null_platform.cpp d9db622766066b8b54393cca91bb9ff2b223c3017730c02344ca56779a3fc66c 495150e39b0a05fe7408f2d48675dcad5c745b2236a992c944989c1616f71a12
src/platform/sdl3/sdl3_platform.cpp 197f1a00e49ba0187f12ea3c372e9db5d50e6ea2e11e841ecea15a0d9129e1e8 3cf51a895ac5e8105442825c2cc68ae94c614c457d4db443087370b12aa1248d
src/platform/sdl3/macos_settings.h f383878faf604fbf3e75c1f654b1c183f89e7667e2e1eb0137b4cd095eccea79 84fbd391c76b9434263f42f389ace96c81ab64d886b906383e4ab069880c6156
src/platform/sdl3/macos_settings.mm b0c35f719abce0c97e6adc5c740cded47ec0c601fc1398043fc444b8c22182e5 f362c4de2ef825d94937d347ff9597f1c70459a4ade6097766e3166c9802f0bf
src/platform/sdl3/macos_settings_stub.cpp 42bd8958346eb8768770e1aed8161a9b58c7060ac119ff2d32abcfd6ac4b3277 66d03e49f09573ef027024140b5a93cbac16c35b558ce375d1d00ea71e6e282e
tests/diff.py 4a178b48b6083a337daa0f7bdb0737070dcd292979ef583dcb0519e6eb379dd9 4a178b48b6083a337daa0f7bdb0737070dcd292979ef583dcb0519e6eb379dd9
tests/diff.sh 93fd55dd6bd792361d1bd93e0b4eb8093832d4f69fafac870c59c5e139ee7ffe eba2037bf8d5aa61f9f7536ba7e01b69da364340bbdde3f42e522f08b4c74ef9
tests/unit/cpu_test.cpp 8e9c99b1f77c0899a40bb560761f5153eea48c7f43861e0231fcf49f29d75f73 e4ebca8b1084e65ee982a9c998d1293e014c5fbe4af0743f0a7f7585e893f8df
tests/unit/input_bindings_test.cpp c35412fd8a2681b3da5bb8988f21aae2ad0a1b767924d8a0b39acc9414ab1158 385cd0d457726c70bdead7efbebd02d36cf228cce1e4637dd7cf5921caeba608
tests/unit/save_store_test.cpp 1949f263e19692597ad674d75b3f2657e72bcb358103fded362862b156ebb528 f323110b7b41a63a0b86951d3a7340080a14bb2404a1822762e7c75fe8028917
tests/unit/settings_test.cpp 815f4b1ab54ac950a953928877367680ed9e04c907ad65d6acb555002d3a035b 5e4801838e95f75a728d0285eefd0af1585e5dc0da80ee122821db6dd9b5522f
tests/unit/text_entry_test.cpp bcfa32bda387386e1df869f05ec87a2021246785b0cf4a1d410173520c7ea0bd 256272cd068a76e69feb2f2bfe0b2959d2b031398b316f0150270223e7783879
tests/unit/wav_test.cpp 2a15b7ea184c03fe3a86d5e0452a2a73f829e82473cde099e3c9330fc61d0fd1 d862d6b503dc904c75194abd736634dc06fb2f9ae0434095cd6cddd8e38bf3b9
.clang-format e395beaa072bb3a70b75d4cfbceb11ab920c7bfbee25bac1e65ed926c72b3c18 e395beaa072bb3a70b75d4cfbceb11ab920c7bfbee25bac1e65ed926c72b3c18
.gitignore 63be401b60760b75af4dd9615260dd5aefbe6be9673f37b4bcbc03dcd78df52d 63be401b60760b75af4dd9615260dd5aefbe6be9673f37b4bcbc03dcd78df52d
CMakeLists.txt e790502de50f2ec5e895aa989de73182383ddcc72fc316d546ff9c30cd4ed97c 0ca930497a6dd4f20efa89b8098b6af248715a8798fd09facc97483a78476e4e
pyproject.toml 4ac3703cc28f121ff2896cc5d5fb4c27af47349f19ea58b3dd30e74d815e33f9 4ac3703cc28f121ff2896cc5d5fb4c27af47349f19ea58b3dd30e74d815e33f9
src/runtime/arm_runtime.cpp 7e67c4e474fe02393638405eef25a5d99a2cd76453f42b46cda912807e192935 44f0c136b6160b3da42a5dceda7b8f7fc22f96aeb8e87896fe1b3cac4db2bf5e
src/runtime/arm_runtime.h 3a9746475421d4e3f7459e865146f68518876922c103352357cdc6b981e5538f 5864cf202ce588840503b7d4795e6ed929941444f178ad8166a2248a61cb566b
-->
