# Ported from the Vortex recomp

Written by `tools/port-from-vortex.py`; do not edit by hand. Every file here entered this
tree as a copy of the file named in the first column, with the identifier rewrites that
script applies. `source` is the SHA-256 of the Vortex original at the moment of the
port; `ported` is the SHA-256 of this tree's copy immediately after it — so a file whose
current hash differs from `ported` has been edited here, and one whose Vortex
original differs from `source` has moved on there. A row whose file is absent here was
ported and then deliberately removed; the reason is in the script's `PORTED` list.

`python3 tools/port-from-vortex.py --check` reports both.

Source tree: `/Users/lucille/Documents/Projects/iPod-Reverse-Engineer/ipod-emulator/recomps/Vortex`

| file | source SHA-256 | ported SHA-256 |
|---|---|---|
| `tools/survey.py` | `54c2ae55cc63b618…` | `54c2ae55cc63b618…` |
| `tools/funcs.py` | `cc3c86675fd2bc41…` | `9fdf46b6b1246526…` |
| `tools/emit.py` | `b3417083aadb8ce7…` | `333ce85dee0ce736…` |
| `tools/progress.py` | `797c1c6d8af03480…` | `797c1c6d8af03480…` |
| `tools/manifest.py` | `3d69c7f483e5c40e…` | `9b6e581a870e1dc6…` |
| `tools/ppm2png.py` | `4428bbf408271143…` | `4428bbf408271143…` |
| `tools/probe.sh` | `d08281fc7ff73658…` | `d08281fc7ff73658…` |
| `src/gamedata/install.cpp` | `7ebbd0c0ed447770…` | `759979a0d92adc32…` |
| `src/gamedata/install.h` | `dad82799efcd0519…` | `86d0a0f0ba02e986…` |
| `src/gamedata/manifest.h` | `8b639b0b6b44375f…` | `2b36badec4d5a973…` |
| `src/gamedata/zip.h` | `1c4583dca7d88749…` | `5198d2f6edacc4d3…` |
| `src/runtime/arm_runtime.json` | `a483df0cd9059faa…` | `a483df0cd9059faa…` |
| `src/runtime/cpu.h` | `cf0b429633377705…` | `208e8f334e9ca3f5…` |
| `src/runtime/eapp_image.cpp` | `33c6937e825ff838…` | `a5d5ae6a0d3d7f6c…` |
| `src/runtime/eapp_image.h` | `aeb15ab77909ba6a…` | `8b80a8c27248cbd6…` |
| `src/runtime/main.cpp` | `66f25c31df9cabc9…` | `380c28d0488ea9b3…` |
| `src/runtime/memory.cpp` | `e2d6601a89aac6b5…` | `426aca185faeddbd…` |
| `src/runtime/memory.h` | `ff77695e0676ebf9…` | `2e78e39f9ec32393…` |
| `src/runtime/runtime.h` | `bf45351c07dade9e…` | `618f055ffb55e095…` |
| `src/framework/audio.h` | `a364e3591de7b17b…` | `5f19b2921f1e1156…` |
| `src/framework/controls.h` | `5833bca662911946…` | `f84d078d8404f978…` |
| `src/framework/device.h` | `0ff1b2bb9292cbf9…` | `34695f8d24fb638f…` |
| `src/framework/graphics.h` | `640005b5426ff610…` | `9ec7f738785de50d…` |
| `src/framework/music_library.h` | `d004d7a8af4b2247…` | `c9df411c821e8807…` |
| `src/framework/storage.h` | `13b10b6dd83c33b7…` | `aeae54fe743bf093…` |
| `src/framework/types.h` | `35e9fe1d05f93361…` | `0c5b51945720e2bc…` |
| `src/libeapp/include/ipod_eapp.h` | `40a3d11afee86054…` | `0bebe20cd365b02e…` |
| `src/libeapp/arm_abi.cpp` | `5c1d46cb1debc6a7…` | `6737fda5745f130e…` |
| `src/libeapp/async_file.cpp` | `9aa7ec6f6686a6e6…` | `a32aba576201b8e4…` |
| `src/libeapp/audio.cpp` | `2c3a83eeb07e57af…` | `5c1460686336c818…` |
| `src/libeapp/framework_call.cpp` | `07b688e1922bb596…` | `c309d2a9aeb4ecac…` |
| `src/libeapp/heap.h` | `c402efd48427f17e…` | `52597f259df0ebd8…` |
| `src/libeapp/host_state.cpp` | `44afc3ef997c0f51…` | `db530677cb82d2e0…` |
| `src/libeapp/host_state.h` | `a725932ec0bf6312…` | `2d52f8c5d36ab49d…` |
| `src/libeapp/imports.json` | `12670c25fc6f7d94…` | `12670c25fc6f7d94…` |
| `src/libeapp/input.cpp` | `d7538acc35c81bc1…` | `92079b384a81a966…` |
| `src/libeapp/metadata.cpp` | `0e79fd6780ccca1e…` | `5718a6b421e53ae8…` |
| `src/libeapp/misc.cpp` | `cc808f6737a02d1b…` | `9aac8fe07a3d38ed…` |
| `src/platform/input_bindings.cpp` | `751b6ae5c80e3936…` | `4f780aeac9409724…` |
| `src/platform/input_bindings.h` | `d2308a6a0a2e2b66…` | `8f473fa2a6e134b4…` |
| `src/platform/paths.cpp` | `fa32a0abb7fda185…` | `de1ccfc1c9f4944b…` |
| `src/platform/paths.h` | `a006dcb9898c61c6…` | `4a4d7f54189228ab…` |
| `src/platform/platform.h` | `65413e8fa409d788…` | `e6ad72e7232ee41e…` |
| `src/platform/save_store.h` | `6dab43d18e08ecca…` | `3f5275613d8281bf…` |
| `src/platform/settings.cpp` | `9271b1ba6479f140…` | `15e93194baabc121…` |
| `src/platform/settings.h` | `df394be0d9203227…` | `bd07b4ff423c5815…` |
| `src/platform/text_entry.h` | `3f488f3096cb6bc3…` | `1ac614a1b7b82a91…` |
| `src/platform/null/null_platform.cpp` | `7ecc5d24de73f7b7…` | `96c41ea91f5c1c43…` |
| `src/platform/sdl3/sdl3_platform.cpp` | `70152d758be49552…` | `8d6e689cf27fba3f…` |
| `src/platform/sdl3/music_decoder.h` | `e4bb306e08fab68f…` | `e90f979d7d578756…` |
| `src/platform/sdl3/macos_settings.h` | `0669552197c5048b…` | `d5289ae17d154580…` |
| `src/platform/sdl3/macos_settings.mm` | `dd5c7236ff24b8f7…` | `fee0246bddb41dc3…` |
| `src/platform/sdl3/macos_settings_stub.cpp` | `0a896afed187035a…` | `b9b23da44e446b16…` |
| `tests/diff.py` | `7bf44fd91c717c44…` | `7bf44fd91c717c44…` |
| `tests/diff.sh` | `2bc5fd781e6210e0…` | `2a6b5ac501585771…` |
| `tests/frames.py` | `e643f36e26ccfb3b…` | `e643f36e26ccfb3b…` |
| `tests/frames.sh` | `d6309cc029e33f9c…` | `0a78bc03330d1bbc…` |
| `tests/game-dir.sh` | `0feecdb1c3fc4a40…` | `7011e09a55b6f52b…` |
| `tests/record.sh` | `a189cc87587febf0…` | `b09d62c1fa8cfa7a…` |
| `tests/unit/cpu_test.cpp` | `120dbbbde8462c36…` | `02106a21bc005bf4…` |
| `tests/unit/input_bindings_test.cpp` | `55f8d21c8c01b253…` | `189da4e07e52de43…` |
| `tests/unit/install_test.cpp` | `4474d8bd0ebd66d5…` | `9c6a1179dfb8afe1…` |
| `tests/unit/render_scale_test.cpp` | `0c6a564cd0c5cdff…` | `d2e5b76bcfd5536b…` |
| `tests/unit/save_files_test.cpp` | `5836a6691dac72a4…` | `461c17dcf4ba2880…` |
| `tests/unit/save_store_test.cpp` | `61a8d25eb837f5da…` | `4d47deb56972e51f…` |
| `tests/unit/settings_test.cpp` | `06bd15d08ea8061d…` | `a95cd5038dc09975…` |
| `tests/unit/text_entry_test.cpp` | `d750a80704d54d6a…` | `ce904156f2958a36…` |
| `.clang-format` | `e395beaa072bb3a7…` | `e395beaa072bb3a7…` |
| `.gitignore` | `e897d73a18eed22a…` | `e897d73a18eed22a…` |
| `CMakeLists.txt` | `1ea124c3604356f0…` | `0cdad76d5d306085…` |
| `pyproject.toml` | `4ac3703cc28f121f…` | `4ac3703cc28f121f…` |

<!-- full hashes, for the --check command
tools/survey.py 54c2ae55cc63b61830327b94d29fb127bb4c942793656c6286df6e282e8f82a6 54c2ae55cc63b61830327b94d29fb127bb4c942793656c6286df6e282e8f82a6
tools/funcs.py cc3c86675fd2bc41da97059cdbdd3ede6639c93fefaab5e0208112b6b5b16052 9fdf46b6b124652650df4363ee936cc79d81242591505cbab0dab0bcf098c7d8
tools/emit.py b3417083aadb8ce70902fffc8c64c81066a5c309abb85fb28dcae74fb70aa0e1 333ce85dee0ce7368bb346fe3442598b6d27838df4287047a938984bf85fa573
tools/progress.py 797c1c6d8af034805ccb150d9a5ce5456fa1eccd6371930d2dc47c03ed31b721 797c1c6d8af034805ccb150d9a5ce5456fa1eccd6371930d2dc47c03ed31b721
tools/manifest.py 3d69c7f483e5c40ed24989b4faf2c598de1db47226c192193c33fc9324d5271e 9b6e581a870e1dc666cd913963d410ca8f29572d6801a276432609164a787d6d
tools/ppm2png.py 4428bbf408271143ee9fc8aede3665b2280fe1fa15cafa4baf460362b21d3d0c 4428bbf408271143ee9fc8aede3665b2280fe1fa15cafa4baf460362b21d3d0c
tools/probe.sh d08281fc7ff73658ec12a1173668dd9b6a9640de0a8b2ca00e785bad85fd577a d08281fc7ff73658ec12a1173668dd9b6a9640de0a8b2ca00e785bad85fd577a
src/gamedata/install.cpp 7ebbd0c0ed4477707662b4429e7f0af516c1e4d07aaafe03fe0ab592dcef3535 759979a0d92adc32ae11b26ffdc44917dbe4652602195759d7981127a3afd0cd
src/gamedata/install.h dad82799efcd0519931d00c83a470e561c45a025cb75a08b4e2a4627d4cb9607 86d0a0f0ba02e986cd4fffb0b0d672df9547a2f48900518b8e2434543f1dea1b
src/gamedata/manifest.h 8b639b0b6b44375f4d9d6366c6a6fba0b352f8c790654df7e47e31ffa0ce21d2 2b36badec4d5a973d33d7a519d9a8d5438f575d1b7df1b472987c5bc13ca4284
src/gamedata/zip.h 1c4583dca7d88749ca88b03a172071b431acfe3691093deb64d883170997a554 5198d2f6edacc4d3991dbc4e742541419d1e4870b3715d0957a5230378fbc00d
src/runtime/arm_runtime.json a483df0cd9059faa5892e7d3a4465a636fc8aab3b2d55056ddba05cb36ad78f9 a483df0cd9059faa5892e7d3a4465a636fc8aab3b2d55056ddba05cb36ad78f9
src/runtime/cpu.h cf0b429633377705487ae937b81a891e12df49ece1baff468aa7eb87f7cacf18 208e8f334e9ca3f51597d8e67575b53afc81ef91c40fdc103953f68239a0c44d
src/runtime/eapp_image.cpp 33c6937e825ff838b885f83afff9f9a536fd10afde7a75283e0f899685f76075 a5d5ae6a0d3d7f6cd0700377decf7de568ae364d86c06ed409d0636e6c698e4c
src/runtime/eapp_image.h aeb15ab77909ba6ad65947120eda6ecee98a5ebd4eeb3771126d2dd0fd556a7c 8b80a8c27248cbd6a89fb36f16d13054373ff7b95701ee9ae4dbc09ef0c088c5
src/runtime/main.cpp 66f25c31df9cabc9e7da122500fcbf5bee059bda79a8d8562ed6040785360e8e 380c28d0488ea9b343bf4b84252de49c739e507dc4322a2a8b558d9730e21fe5
src/runtime/memory.cpp e2d6601a89aac6b5e265273e9f805470bf54b0ca70d5c6dffe19d996e5bfe8b7 426aca185faeddbd9c55043f60002db4ded793bb90f410294f487f317399b9af
src/runtime/memory.h ff77695e0676ebf9caf8a729353da39e7259d6f2d2f17cce223a8398165ddb24 2e78e39f9ec32393ee8148d96397c05658af6b36f137ff990c8e3e64f66b1c74
src/runtime/runtime.h bf45351c07dade9e8ca75bc14ab57aa200cb49c12da72e66d0d5fad1e49565b6 618f055ffb55e095846e2b4c68834319cc8f98b4a90cc3103708157e80d407f7
src/framework/audio.h a364e3591de7b17be9ef90bf1c4d9547599b083ae65620e7922670e2229aba0d 5f19b2921f1e115687cb27c0bf4b333e353e6f0f20086cab7434b0353be7148b
src/framework/controls.h 5833bca66291194684fe77f1ce7ff287af7c801c06a9da2da3bed0bd5e700cc2 f84d078d8404f97800c5bbca2e6a777d773418c90cb3b3a40560c33d8544e86b
src/framework/device.h 0ff1b2bb9292cbf9d0daa0a28f8a855343ca45ed8156fa175eaa3eb5bb022e43 34695f8d24fb638f9a054859231946cc41cad464efdcdeab998d77e262193eed
src/framework/graphics.h 640005b5426ff610b2c4a6ea81724294c13f01667954fdad5f7bc8335b21a093 9ec7f738785de50d6e6a49e25fcc67f6e7b3dabf86192891d9fcc89d5ddefb37
src/framework/music_library.h d004d7a8af4b22473e98b72a849961d103a5f16a59aed5b5a160377638a37f87 c9df411c821e88073794078fd36b088770e5727028e456bf88d750c54e95244d
src/framework/storage.h 13b10b6dd83c33b7eb2456dc04efda00ea1721c757710d73d4cec1c39ec58400 aeae54fe743bf093046850d4387cb9a98fd5cafcca45b6050fc47a0542e849dc
src/framework/types.h 35e9fe1d05f93361580d043522365752173619c53a91572de54d3db3f9800f34 0c5b51945720e2bc56b021a0959a03dcda7649267620e31cd661672563f61f3e
src/libeapp/include/ipod_eapp.h 40a3d11afee860542aec722e44e7d92a8d9c29be1fd0359196a02b66068a4ce5 0bebe20cd365b02ea80bc5fe782a09a957b807017e0db17140a73f3e477a2286
src/libeapp/arm_abi.cpp 5c1d46cb1debc6a7a67caaf34760e569b12d23682845d8162e629849d363a5b8 6737fda5745f130ea4cd1c9f296e33ddb987dbc11bf7870284551b7ad4f9120a
src/libeapp/async_file.cpp 9aa7ec6f6686a6e6113d2be457064feddf0f3ad21fa703957dced8470b1c3058 a32aba576201b8e424fdb44db17796330b6e657e29f7d370cbb6166e82360364
src/libeapp/audio.cpp 2c3a83eeb07e57af8e717646d927ad63911f768dc5b6a538c0ac818a3ce395bf 5c1460686336c8188690f7fdaee4cbc9c638d1e22d3d5b35c6ebb632e5053f3f
src/libeapp/framework_call.cpp 07b688e1922bb596f0858b63f8d05dd15001552ed0c6916d7d2b8c42f2f97eca c309d2a9aeb4ecac7b638ec12fb0c2adb4ef4490e86ca20d4ba32184e4c4a539
src/libeapp/heap.h c402efd48427f17ea617b328d5559a689ebab4acd32c65437d28a76fc0a3f579 52597f259df0ebd8fabf1f76183d501e74bd192262484e8fe3efb277c640117a
src/libeapp/host_state.cpp 44afc3ef997c0f51c42043ae7fd3e75612fcb4a8e78c19f7652d7e7303c7bc90 db530677cb82d2e05acc422a87784e2345c8c10d8f7af97cf96fa3d80f2aec7e
src/libeapp/host_state.h a725932ec0bf631231a0df08047feb7f27d16387cfd3d096c6a6fec37c25ed6c 2d52f8c5d36ab49d8180fab02f78fff9c5096f04d84249aee668b2b1c023bd1d
src/libeapp/imports.json 12670c25fc6f7d943c36180fe336854580c72c38c281f835bd95723b3049a364 12670c25fc6f7d943c36180fe336854580c72c38c281f835bd95723b3049a364
src/libeapp/input.cpp d7538acc35c81bc1af323945156f70dc0bd1339622af364db7a9df14a56cc2b9 92079b384a81a966f2241b940bbe1ea75d87904e5924166ea5f5ac7741d90555
src/libeapp/metadata.cpp 0e79fd6780ccca1e117c3c3a8cbeeebeb3a7f18d8d7ccf98dce8a36e6e539050 5718a6b421e53ae8f13c934e2b15a42e9d64be1a6c1734cb029ba417ee48be6b
src/libeapp/misc.cpp cc808f6737a02d1ba0503b15c96e02cdc015ce83b5f5308876cf2b7ea0352824 9aac8fe07a3d38edf1f06f29f4f6542f941623a1bfaf227127079bd9f857ca38
src/platform/input_bindings.cpp 751b6ae5c80e3936836cca95f90985d9ee3a2cbc6265d73e358e87d3701f3549 4f780aeac9409724f6de77fa493f5f1f9f69cf03b83c69e9027c8f55039151ee
src/platform/input_bindings.h d2308a6a0a2e2b66e9a72430dc1021d0aaf5a434a4222096e289dd13397b2dc0 8f473fa2a6e134b490d09c34a8a3d908411bec6fb836f04e3faf68ca1ed560a3
src/platform/paths.cpp fa32a0abb7fda185668ff7e1db4413b59f6aa896a7e307441f8b0c3a1ccb4b21 de1ccfc1c9f4944b9acdc68921240bbab28b59af3349705cd452ac1833896d6f
src/platform/paths.h a006dcb9898c61c67f24bdb720d30385faadabfb50dc4c3d74fc26fda92398fc 4a4d7f54189228ab68b01a597005a1d008cec6748f239576f072ec29a663a6b6
src/platform/platform.h 65413e8fa409d78862995013a8c7a71af4d33426347fa8ac107718ba694648f4 e6ad72e7232ee41eaec842e72ecd07571e91ae94cf87fccca2d133fc52e47985
src/platform/save_store.h 6dab43d18e08eccad2df73808068ef4cdf5bf53bb0b1f872c5861bc73d679921 3f5275613d8281bf9bef7d10d6b1339d641a7910e5fbb7682664c67d93835452
src/platform/settings.cpp 9271b1ba6479f140fd66b030d53d5466118053575acc788e521e4c7e8d578d5e 15e93194baabc121f63c02cdd4ffea1c682f26ef16804b6fb403a3891020ab6b
src/platform/settings.h df394be0d9203227e4fd7153159f83a81070b605adfa2e43c973caefc6c37eba bd07b4ff423c58159ae212d3ab08ab0ebff1593e3319de7b7fe75d4ca4f08133
src/platform/text_entry.h 3f488f3096cb6bc31e33b32df0341ceaad47beeca71007b2eae98c91130dfcc3 1ac614a1b7b82a91becbc106d43d8f5e1ea26c26b967a5127ff1f88a00bf5862
src/platform/null/null_platform.cpp 7ecc5d24de73f7b75aff06df1e3dec3d10330aa447e41fefa79a2d4e96eaa8ca 96c41ea91f5c1c43293cc7344009077f1db0ea9071c88ca09bc6a4df0306b1f4
src/platform/sdl3/sdl3_platform.cpp 70152d758be495528002030fe18f364e6c19d3f5b7f49fddc6e3bc47473fc947 8d6e689cf27fba3f7560e2fb005beb5e48a0be2b5bec6c9912aaec5936eb0bb5
src/platform/sdl3/music_decoder.h e4bb306e08fab68f1dacb62181de21e1fd89dd3b99cfe6e4c0f0df35f908a049 e90f979d7d578756da82b13dd1a0483ead7f1c6c8889a5c3fb53aef0b8d639c1
src/platform/sdl3/macos_settings.h 0669552197c5048b986557834147b9f1723ccefdf7f08183385b01d8a826b142 d5289ae17d1545801b5896bda79712020761de1e479d61ccb6e10f623084df6f
src/platform/sdl3/macos_settings.mm dd5c7236ff24b8f7d948d56f3fdb8ebab31a4de5760bec660466338b39b54b1b fee0246bddb41dc3448945ef34deab17f7225c5d01e12a306ee797d1516a280e
src/platform/sdl3/macos_settings_stub.cpp 0a896afed187035a106ba379f401673270dbd9290c75287c33ad82af01f6ada0 b9b23da44e446b16f5ec055b5bf8e64dfc1dfeb450bd25aa4dfe52aea95a027d
tests/diff.py 7bf44fd91c717c44adec53a2364198fbbf07e0f1ad17548d114783a7786148da 7bf44fd91c717c44adec53a2364198fbbf07e0f1ad17548d114783a7786148da
tests/diff.sh 2bc5fd781e6210e004b458148e1be1d74c69c528d85ba2369d2ae6780155744b 2a6b5ac50158577139c06c0ae36ac6662ab02f295173949608e2389f27b902b4
tests/frames.py e643f36e26ccfb3b2ebd09fff235a1245313c3932b5e18a41e0c7dc7afc2110c e643f36e26ccfb3b2ebd09fff235a1245313c3932b5e18a41e0c7dc7afc2110c
tests/frames.sh d6309cc029e33f9c527db8ad2e00db2a319c3f51c3ff10f88671e690a1a32129 0a78bc03330d1bbc846a19b8a30b9558ea9ab9ae102a2fac00af5bc28a20c836
tests/game-dir.sh 0feecdb1c3fc4a40177a262486fefbe51f1b87706bb070ab7714fde773908f0f 7011e09a55b6f52bb3b8b100390b65c9bf95029cb398f73928ea862e44ee506d
tests/record.sh a189cc87587febf0da8a00298f5c82d325532e6b06a126f6b763441c807a57aa b09d62c1fa8cfa7a2d9fb9bce6b0cfc8253f054527523646b220c08baefec350
tests/unit/cpu_test.cpp 120dbbbde8462c366eede54b9ffbc1060dc9966ec09f975d30457bff0b711ec5 02106a21bc005bf4a3ebb2d05c9541091af4ba8a1e0e535d04e2beec3680ded0
tests/unit/input_bindings_test.cpp 55f8d21c8c01b253631b217829a9b72e202213eb5fadd7841f922b7ede3f853d 189da4e07e52de43ab9dcbc4ce924e61ad4b49ab37dc43bb42566e2e7dba8ef6
tests/unit/install_test.cpp 4474d8bd0ebd66d54e6f9b3b2648a528d03e7de241f6af7164dbdc17519bd1f9 9c6a1179dfb8afe1650f39934fce0e43f166a97e8703a4b3dc4adfd1beaa7193
tests/unit/render_scale_test.cpp 0c6a564cd0c5cdff239d8dedf0c79365faddfe6ad1fa71c17452285f1759e314 d2e5b76bcfd5536bc54ab80396022ec5f26329e9c65db87e5b36a06402d4c23a
tests/unit/save_files_test.cpp 5836a6691dac72a4939d3b2f258d38fda5003e1bd90f8ad5ec2d9cee7e488ecb 461c17dcf4ba2880fe937320ddcff31f01869f9e8e058e5bc4fbe053d1aedd06
tests/unit/save_store_test.cpp 61a8d25eb837f5dae16fc79435e6c2a70cb86085587dd6d78c1d4b04b3eb0a54 4d47deb56972e51fbcaa56e4dd414bc6785b4ad6081ef3f74d2a5b7dfcd62810
tests/unit/settings_test.cpp 06bd15d08ea8061d15489b8f3298910a3b2b368a7dfa1fc7e39d3b7f4cb849a2 a95cd5038dc0997549a003a0d145cd5903cfefb7149f3eb1e4f6b9a38ae021e7
tests/unit/text_entry_test.cpp d750a80704d54d6ac68de18b94d7df5e616bc6f080b0203c6beaf3d51561df3b ce904156f2958a36134d6a2750d2ec95d3a9348afb23594c6bb7331d13745113
.clang-format e395beaa072bb3a70b75d4cfbceb11ab920c7bfbee25bac1e65ed926c72b3c18 e395beaa072bb3a70b75d4cfbceb11ab920c7bfbee25bac1e65ed926c72b3c18
.gitignore e897d73a18eed22a2bdf122cdb106da753b2e5d0759f75eb140b009a8641cb44 e897d73a18eed22a2bdf122cdb106da753b2e5d0759f75eb140b009a8641cb44
CMakeLists.txt 1ea124c3604356f08dfe3ad26c1948767578753c43b0c1d58654a621d34e0278 0cdad76d5d30608550e5e5422ef2fc7283a1a7568150fefaee125d942d105d71
pyproject.toml 4ac3703cc28f121ff2896cc5d5fb4c27af47349f19ea58b3dd30e74d815e33f9 4ac3703cc28f121ff2896cc5d5fb4c27af47349f19ea58b3dd30e74d815e33f9
-->
