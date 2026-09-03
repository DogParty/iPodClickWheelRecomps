# Ported from The Sims Bowling recomp

Written by `tools/port-from-bowling.py`; do not edit by hand. Every file here entered this
tree as a copy of the file named in the first column, with the identifier rewrites that
script applies. `source` is the SHA-256 of the Sims Bowling original at the moment of the
port; `ported` is the SHA-256 of this tree's copy immediately after it — so a file whose
current hash differs from `ported` has been edited here, and one whose Sims Bowling
original differs from `source` has moved on there. A row whose file is absent here was
ported and then deliberately removed; the reason is in the script's `PORTED` list.

`python3 tools/port-from-bowling.py --check` reports both.

Source tree: `../Sims Bowling`

| file | source SHA-256 | ported SHA-256 |
|---|---|---|
| `tools/survey.py` | `54c2ae55cc63b618…` | `54c2ae55cc63b618…` |
| `tools/funcs.py` | `c4e1cb4c00412689…` | `c4e1cb4c00412689…` |
| `tools/emit.py` | `559c1ada586474e8…` | `b3417083aadb8ce7…` |
| `tools/progress.py` | `797c1c6d8af03480…` | `797c1c6d8af03480…` |
| `tools/manifest.py` | `d5b677bd3cfe3ade…` | `eccb3ac628322aa1…` |
| `tools/ppm2png.py` | `4428bbf408271143…` | `4428bbf408271143…` |
| `src/gamedata/install.cpp` | `97af690fa1afa697…` | `286e61dea2e9cb78…` |
| `src/gamedata/install.h` | `4545c01d85ae7137…` | `50474fb338575ab8…` |
| `src/gamedata/manifest.h` | `c6faf0dfd5dadf30…` | `fe567d01bd34e77b…` |
| `src/gamedata/zip.h` | `9a056e48aeda2897…` | `1c4583dca7d88749…` |
| `src/runtime/arm_runtime.json` | `a483df0cd9059faa…` | `a483df0cd9059faa…` |
| `src/runtime/cpu.h` | `970982544db4a77a…` | `cf0b429633377705…` |
| `src/runtime/eapp_image.cpp` | `f11712d5d4ff6c7c…` | `33c6937e825ff838…` |
| `src/runtime/eapp_image.h` | `b915497b08acf890…` | `aeb15ab77909ba6a…` |
| `src/runtime/main.cpp` | `49995e55ad080490…` | `1c26c8ba634759bf…` |
| `src/runtime/memory.cpp` | `409a0331a4c3093a…` | `e2d6601a89aac6b5…` |
| `src/runtime/memory.h` | `1a2f850e6573f6be…` | `ff77695e0676ebf9…` |
| `src/runtime/runtime.h` | `75bf1c4f1a1d5eec…` | `bf45351c07dade9e…` |
| `src/framework/audio.h` | `924b777a36fbb047…` | `a364e3591de7b17b…` |
| `src/framework/controls.h` | `fd7b30bf6e9a3beb…` | `5833bca662911946…` |
| `src/framework/device.h` | `e6e635d6a471dc1e…` | `a9f49733bef211a1…` |
| `src/framework/graphics.h` | `41c36ca43c3902ba…` | `640005b5426ff610…` |
| `src/framework/music_library.h` | `be647635376ce827…` | `dda2184c02a41c3b…` |
| `src/framework/storage.h` | `fc931460bae64f81…` | `13b10b6dd83c33b7…` |
| `src/framework/types.h` | `17dd55de3c7e6421…` | `35e9fe1d05f93361…` |
| `src/libeapp/include/ipod_eapp.h` | `9484a8d089cc3d79…` | `2d39b0e74ee8ea78…` |
| `src/libeapp/arm_abi.cpp` | `a1e18ebc8f074d02…` | `962312f2209bc43a…` |
| `src/libeapp/async_file.cpp` | `8d119a5c48bf5811…` | `50e31a5ea41b039c…` |
| `src/libeapp/audio.cpp` | `912f24d3caa044a2…` | `2c3a83eeb07e57af…` |
| `src/libeapp/framework_call.cpp` | `12fb82270c577a27…` | `07b688e1922bb596…` |
| `src/libeapp/heap.h` | `508967032e9f5f9b…` | `c402efd48427f17e…` |
| `src/libeapp/host_state.cpp` | `9d501fb4c9560ce3…` | `44afc3ef997c0f51…` |
| `src/libeapp/host_state.h` | `ccc6ceff0f21f355…` | `a725932ec0bf6312…` |
| `src/libeapp/imports.json` | `245f6ce123a938a4…` | `245f6ce123a938a4…` |
| `src/libeapp/input.cpp` | `4439988949e82281…` | `d7538acc35c81bc1…` |
| `src/libeapp/metadata.cpp` | `244ac02ae5e1f8a3…` | `139bb5acbf4c036c…` |
| `src/libeapp/misc.cpp` | `23a8d3caf9f18666…` | `05bea848468e8bf5…` |
| `src/platform/input_bindings.cpp` | `2376556483dbd824…` | `9968c84af5c7e507…` |
| `src/platform/input_bindings.h` | `4a6da86931a968ea…` | `d2308a6a0a2e2b66…` |
| `src/platform/paths.cpp` | `caabffc28bd21993…` | `fa32a0abb7fda185…` |
| `src/platform/paths.h` | `f5e713e91b9d2f79…` | `a006dcb9898c61c6…` |
| `src/platform/platform.h` | `ea77a898af22c754…` | `65413e8fa409d788…` |
| `src/platform/save_store.h` | `569782b6cccd115d…` | `6dab43d18e08ecca…` |
| `src/platform/settings.cpp` | `11400bb4f9b8110d…` | `188a13ce23bccfda…` |
| `src/platform/settings.h` | `dd446c52b2594cf5…` | `94d794faad4bdf0c…` |
| `src/platform/text_entry.h` | `e82e01e1e0e14a22…` | `3f488f3096cb6bc3…` |
| `src/platform/null/null_platform.cpp` | `6bbe6bc004244457…` | `7ecc5d24de73f7b7…` |
| `src/platform/sdl3/sdl3_platform.cpp` | `5debd50ead52a26d…` | `70152d758be49552…` |
| `src/platform/sdl3/music_decoder.h` | `b88fdd559545f338…` | `e4bb306e08fab68f…` |
| `src/platform/sdl3/macos_settings.h` | `4db4f4c08de92034…` | `0669552197c5048b…` |
| `src/platform/sdl3/macos_settings.mm` | `1847322956c5d98c…` | `dd5c7236ff24b8f7…` |
| `src/platform/sdl3/macos_settings_stub.cpp` | `b847c4ceaa735e9b…` | `0a896afed187035a…` |
| `tests/diff.py` | `4a178b48b6083a33…` | `4a178b48b6083a33…` |
| `tests/diff.sh` | `61fa7cd11dc70d53…` | `14d6ac19fec80b40…` |
| `tests/frames.py` | `b6395991fb0d22e4…` | `b6395991fb0d22e4…` |
| `tests/frames.sh` | `5433743f90744110…` | `aa4d0fcbeee500e3…` |
| `tests/game-dir.sh` | `afa4f08745c5ed29…` | `afa4f08745c5ed29…` |
| `tests/record.sh` | `4e95726f3f7586d1…` | `4e95726f3f7586d1…` |
| `tests/unit/cpu_test.cpp` | `82e41f9b6573d07d…` | `120dbbbde8462c36…` |
| `tests/unit/input_bindings_test.cpp` | `89716b5cc6e69e9e…` | `55f8d21c8c01b253…` |
| `tests/unit/install_test.cpp` | `2bd76a7ae9f119a2…` | `d5e13f601d9a3b8e…` |
| `tests/unit/render_scale_test.cpp` | `a50c4526377c5d51…` | `0c6a564cd0c5cdff…` |
| `tests/unit/save_files_test.cpp` | `1df08f37b4b83ffa…` | `d680a13f05098eb6…` |
| `tests/unit/save_store_test.cpp` | `17113cbdd8e5fa2d…` | `61a8d25eb837f5da…` |
| `tests/unit/settings_test.cpp` | `440cc8409bb268b3…` | `ac00772d7788adcc…` |
| `tests/unit/text_entry_test.cpp` | `18e4a77ecf14d4ea…` | `d750a80704d54d6a…` |
| `.clang-format` | `e395beaa072bb3a7…` | `e395beaa072bb3a7…` |
| `.gitignore` | `e897d73a18eed22a…` | `e897d73a18eed22a…` |
| `CMakeLists.txt` | `adc4e2de6e085f4e…` | `a839e6380eca2036…` |
| `pyproject.toml` | `4ac3703cc28f121f…` | `4ac3703cc28f121f…` |

<!-- full hashes, for the --check command
tools/survey.py 54c2ae55cc63b61830327b94d29fb127bb4c942793656c6286df6e282e8f82a6 54c2ae55cc63b61830327b94d29fb127bb4c942793656c6286df6e282e8f82a6
tools/funcs.py c4e1cb4c0041268969e1f19408949fc6778ecdd6cf70b2488d0423a9d674b4b7 c4e1cb4c0041268969e1f19408949fc6778ecdd6cf70b2488d0423a9d674b4b7
tools/emit.py 559c1ada586474e8eba55e8bcaf9cdbebd1fb642c201597ba67ad4962e1ae2d0 b3417083aadb8ce70902fffc8c64c81066a5c309abb85fb28dcae74fb70aa0e1
tools/progress.py 797c1c6d8af034805ccb150d9a5ce5456fa1eccd6371930d2dc47c03ed31b721 797c1c6d8af034805ccb150d9a5ce5456fa1eccd6371930d2dc47c03ed31b721
tools/manifest.py d5b677bd3cfe3ade9f88e0964cbfe831903e2d18942dec1c90c07135d83ea574 eccb3ac628322aa16e87f466a9578a886f98a7578324f9b2030b7e12e3239c6d
tools/ppm2png.py 4428bbf408271143ee9fc8aede3665b2280fe1fa15cafa4baf460362b21d3d0c 4428bbf408271143ee9fc8aede3665b2280fe1fa15cafa4baf460362b21d3d0c
src/gamedata/install.cpp 97af690fa1afa697fe3aa9ee2596c2ad52cdf61fc6a3c943204d771b68f046d0 286e61dea2e9cb781c89c2d8da8846a58e028eb3cbbfbe101c3e54c41ece953d
src/gamedata/install.h 4545c01d85ae71377eddad90e5cce98ae2464cd9f17b0cbaaa4671c2d44e7630 50474fb338575ab873dd07a1c404a81f895c460d1295615c4d6ac209c7ce8ce5
src/gamedata/manifest.h c6faf0dfd5dadf30c786a45ead673141df9034f72dbadb04cc82fb8da2fec215 fe567d01bd34e77bf634d113e210c978a662875b6538c9b5d41217c1e68cb9ef
src/gamedata/zip.h 9a056e48aeda28974bb7833883031407fa9023c11bf441889e71ceed2b046bdf 1c4583dca7d88749ca88b03a172071b431acfe3691093deb64d883170997a554
src/runtime/arm_runtime.json a483df0cd9059faa5892e7d3a4465a636fc8aab3b2d55056ddba05cb36ad78f9 a483df0cd9059faa5892e7d3a4465a636fc8aab3b2d55056ddba05cb36ad78f9
src/runtime/cpu.h 970982544db4a77ab84e1263658bb02bdff0cbcd766b261725ffcfe49af8e927 cf0b429633377705487ae937b81a891e12df49ece1baff468aa7eb87f7cacf18
src/runtime/eapp_image.cpp f11712d5d4ff6c7ce2a818e8764c302ec806890a38bfd0861cfc3d4ca239e15f 33c6937e825ff838b885f83afff9f9a536fd10afde7a75283e0f899685f76075
src/runtime/eapp_image.h b915497b08acf890972b269e1fb1a47b6d84fa4cc43ba9b0f9db1f7a23d2c56d aeb15ab77909ba6ad65947120eda6ecee98a5ebd4eeb3771126d2dd0fd556a7c
src/runtime/main.cpp 49995e55ad080490f998bb2af2b6d25e91f99f50feae0ce95ac880ff23905dd0 1c26c8ba634759bfd70678d46313db75b1ae806523cf60a9675ca6525778e552
src/runtime/memory.cpp 409a0331a4c3093a36ac99187c03b2e8a1345cda8b14ffc1c670c367617e4c4f e2d6601a89aac6b5e265273e9f805470bf54b0ca70d5c6dffe19d996e5bfe8b7
src/runtime/memory.h 1a2f850e6573f6be3112410927613b0dfe652604c4d91b47905a1d56c19c83a3 ff77695e0676ebf9caf8a729353da39e7259d6f2d2f17cce223a8398165ddb24
src/runtime/runtime.h 75bf1c4f1a1d5eec0a853472931c2ed50f7b0a91c7fc0fe82a1324e08727c950 bf45351c07dade9e8ca75bc14ab57aa200cb49c12da72e66d0d5fad1e49565b6
src/framework/audio.h 924b777a36fbb04775245d47e7b345b3646e972649afad96fd80a29b84699e05 a364e3591de7b17be9ef90bf1c4d9547599b083ae65620e7922670e2229aba0d
src/framework/controls.h fd7b30bf6e9a3bebcdbf3d7c723405b69e7fb94d47854c05560b248bc28630fa 5833bca66291194684fe77f1ce7ff287af7c801c06a9da2da3bed0bd5e700cc2
src/framework/device.h e6e635d6a471dc1ea1edfd6a3cfd2e1c17b1b1c6d3531c0fba7bde65674b521b a9f49733bef211a13b38a9f0cd0e4134950afe1550a28f726b8980677c2f78da
src/framework/graphics.h 41c36ca43c3902ba7fc9c417ecc6bfb4670f70bade97169cd0b67c5dd017e5bc 640005b5426ff610b2c4a6ea81724294c13f01667954fdad5f7bc8335b21a093
src/framework/music_library.h be647635376ce827b5e75a69f00742e5c1ca33ccbe5e88b3264485ec03233eb7 dda2184c02a41c3bb234af3bd12644adf8f9748d8dacfcafcce27acec7765f9c
src/framework/storage.h fc931460bae64f816816d03a40a017e4ecac9de297f19c28682a477a3850a0ad 13b10b6dd83c33b7eb2456dc04efda00ea1721c757710d73d4cec1c39ec58400
src/framework/types.h 17dd55de3c7e642172430fd821e673e38cf853708ef1ad7f6a48cb8107d31241 35e9fe1d05f93361580d043522365752173619c53a91572de54d3db3f9800f34
src/libeapp/include/ipod_eapp.h 9484a8d089cc3d792f0db4f86d440975767c192b8a9f1bde6065e03e87bc2bd4 2d39b0e74ee8ea78c3b89acc4c44d5db7246b0e1dbb4c909779b90c8acd48b00
src/libeapp/arm_abi.cpp a1e18ebc8f074d02408446fcfa7b5ac64a1d1e263bdbdbb9942b84f11a5e4137 962312f2209bc43a6790bf2d727474fa151eb04c683420f5988f5f9eda3c9978
src/libeapp/async_file.cpp 8d119a5c48bf5811b72530442c10cb8f86cc8a19651e693196d388db945857a8 50e31a5ea41b039ca838236534b64550222022b094f323dad3936bd5d3b77915
src/libeapp/audio.cpp 912f24d3caa044a2e0eab022dceb384044861401640fc24bc63f4bad2760d3a5 2c3a83eeb07e57af8e717646d927ad63911f768dc5b6a538c0ac818a3ce395bf
src/libeapp/framework_call.cpp 12fb82270c577a276ab14ce84bb099c670892da7e455192bb95002dc3e17d2b8 07b688e1922bb596f0858b63f8d05dd15001552ed0c6916d7d2b8c42f2f97eca
src/libeapp/heap.h 508967032e9f5f9bd9cfe63bddbd530d60f88ff0a3264a3bb35e9b3a97dc6511 c402efd48427f17ea617b328d5559a689ebab4acd32c65437d28a76fc0a3f579
src/libeapp/host_state.cpp 9d501fb4c9560ce38d0a4a0c09b94cab233eb4ad8e194cc39e3f864f23328a55 44afc3ef997c0f51c42043ae7fd3e75612fcb4a8e78c19f7652d7e7303c7bc90
src/libeapp/host_state.h ccc6ceff0f21f355114221935a66d13582cca877c3aa45d8baafa17a5583e388 a725932ec0bf631231a0df08047feb7f27d16387cfd3d096c6a6fec37c25ed6c
src/libeapp/imports.json 245f6ce123a938a48cd7680f2936f34ddb482cc0007db915425a933812674072 245f6ce123a938a48cd7680f2936f34ddb482cc0007db915425a933812674072
src/libeapp/input.cpp 4439988949e82281d54232406f7b351dbfd9716924332f8eae66aae1f366f7cf d7538acc35c81bc1af323945156f70dc0bd1339622af364db7a9df14a56cc2b9
src/libeapp/metadata.cpp 244ac02ae5e1f8a3aae3aac16c3d2f7aace4998562019d76e01c57edbdd6da36 139bb5acbf4c036c6936bff2d6776cabba9643fa1f276680094df694d0906a8a
src/libeapp/misc.cpp 23a8d3caf9f186665de840c65fbe08b2206f20221ad57c6a0bb2d3d86bddf8cb 05bea848468e8bf5e1d1e846ff538c62feacc1f754ffcfe8dcf8d0322d66ea35
src/platform/input_bindings.cpp 2376556483dbd824d11613e02366fbde76c6a22bd363148c32a20c01653cad33 9968c84af5c7e50716072619a720211dd07360c6560655f18e1526042282c64c
src/platform/input_bindings.h 4a6da86931a968eacd36dd7988ce8e0ab7660711967d524cc11ab3dbe2128ae5 d2308a6a0a2e2b66e9a72430dc1021d0aaf5a434a4222096e289dd13397b2dc0
src/platform/paths.cpp caabffc28bd21993ff0e1008a7fb334b8418f83a77623b0939ba31f9534bc6e1 fa32a0abb7fda185668ff7e1db4413b59f6aa896a7e307441f8b0c3a1ccb4b21
src/platform/paths.h f5e713e91b9d2f7920642e6d603db5ac104055a09a8e7d41b59f27cc62d09839 a006dcb9898c61c67f24bdb720d30385faadabfb50dc4c3d74fc26fda92398fc
src/platform/platform.h ea77a898af22c754ab168a6e1588a361981f8f4863dc7f1d5590e0b04bb3c4e3 65413e8fa409d78862995013a8c7a71af4d33426347fa8ac107718ba694648f4
src/platform/save_store.h 569782b6cccd115dcc19c711a0dbe8ec9c80990dd4c8e6e3be0793278fb4f465 6dab43d18e08eccad2df73808068ef4cdf5bf53bb0b1f872c5861bc73d679921
src/platform/settings.cpp 11400bb4f9b8110d00aebc491a6429ed25e09a4fc03c7854da779fdfa97e30e8 188a13ce23bccfda70f418b5eaf4776bdc0f56df327c14665b158405441d167c
src/platform/settings.h dd446c52b2594cf5d37e1bc87e3bb865a7483a7111513d76901875978b88aba8 94d794faad4bdf0c0d08aaf347b3c01f631820898a5b2686e43b37b5cbd2832e
src/platform/text_entry.h e82e01e1e0e14a223aa13af1009dbce38ce0bb5326f39776a70103c4027a3e8c 3f488f3096cb6bc31e33b32df0341ceaad47beeca71007b2eae98c91130dfcc3
src/platform/null/null_platform.cpp 6bbe6bc0042444575b3b144d8cdb169c263acf004e008d1842be68eb31514d0b 7ecc5d24de73f7b75aff06df1e3dec3d10330aa447e41fefa79a2d4e96eaa8ca
src/platform/sdl3/sdl3_platform.cpp 5debd50ead52a26dc30002413258b024920f2b5d1064aff4eb15e2b37ef1bc48 70152d758be495528002030fe18f364e6c19d3f5b7f49fddc6e3bc47473fc947
src/platform/sdl3/music_decoder.h b88fdd559545f3382edfc1914e841551f107a7b1d12fde8290efa615f4f44c34 e4bb306e08fab68f1dacb62181de21e1fd89dd3b99cfe6e4c0f0df35f908a049
src/platform/sdl3/macos_settings.h 4db4f4c08de92034b987b80d44e250e9f342c3b40ce7976251e42921592dd6c4 0669552197c5048b986557834147b9f1723ccefdf7f08183385b01d8a826b142
src/platform/sdl3/macos_settings.mm 1847322956c5d98cf45041b3a6855abefd8d8c90ef9d644ecf84ac1ee0c69f89 dd5c7236ff24b8f7d948d56f3fdb8ebab31a4de5760bec660466338b39b54b1b
src/platform/sdl3/macos_settings_stub.cpp b847c4ceaa735e9b6a95a979481e418450dcdd8f8d81c1b62b4b9d4395aa2d4d 0a896afed187035a106ba379f401673270dbd9290c75287c33ad82af01f6ada0
tests/diff.py 4a178b48b6083a337daa0f7bdb0737070dcd292979ef583dcb0519e6eb379dd9 4a178b48b6083a337daa0f7bdb0737070dcd292979ef583dcb0519e6eb379dd9
tests/diff.sh 61fa7cd11dc70d53811c11187ee7371de16e82f50dee94dfcfff3a61ea90a711 14d6ac19fec80b404510bd195578f1334340bdaf5232c633e612b864ca651b3f
tests/frames.py b6395991fb0d22e405a1b0ec9c78a6f57f53d87bb7fd9c1db878b0ad2205dec6 b6395991fb0d22e405a1b0ec9c78a6f57f53d87bb7fd9c1db878b0ad2205dec6
tests/frames.sh 5433743f907441102e2a1ccbdc50de8e094eebe34eca8f136050ef7e0d127b83 aa4d0fcbeee500e382e707bd15874438574fcf486ba80cc2d0bcd220fe576d44
tests/game-dir.sh afa4f08745c5ed29c6c1c814e286034fddd9c7c3bdf5147b335a3ace7334776c afa4f08745c5ed29c6c1c814e286034fddd9c7c3bdf5147b335a3ace7334776c
tests/record.sh 4e95726f3f7586d142e34d2cd5f445c90cbfededea6048c77a8179475b06341d 4e95726f3f7586d142e34d2cd5f445c90cbfededea6048c77a8179475b06341d
tests/unit/cpu_test.cpp 82e41f9b6573d07ddd8701a8a727a4184d1f138fd2aa7c0d2adc2ce870b22cbf 120dbbbde8462c366eede54b9ffbc1060dc9966ec09f975d30457bff0b711ec5
tests/unit/input_bindings_test.cpp 89716b5cc6e69e9efddf9cae0de51bcb553879325f1ddd03fa0d3d11949341da 55f8d21c8c01b253631b217829a9b72e202213eb5fadd7841f922b7ede3f853d
tests/unit/install_test.cpp 2bd76a7ae9f119a22847886e4c5de9e538e97c79d1852c4295c48b1db701d682 d5e13f601d9a3b8e72b8a0485e2bcc3b6276bda8057852fa2844096edfb3be07
tests/unit/render_scale_test.cpp a50c4526377c5d518767debfe17ab166276c01aed58ceab6fa520d7fd878db66 0c6a564cd0c5cdff239d8dedf0c79365faddfe6ad1fa71c17452285f1759e314
tests/unit/save_files_test.cpp 1df08f37b4b83ffae7db8443c09a3d744b432ef20d7e22719888aed44118367b d680a13f05098eb6bac9935b7d246d69543c21f741d97051321477f976d6af04
tests/unit/save_store_test.cpp 17113cbdd8e5fa2de27a52d8886ba4177d6a11c7587a8eb1b32654eed354b97c 61a8d25eb837f5dae16fc79435e6c2a70cb86085587dd6d78c1d4b04b3eb0a54
tests/unit/settings_test.cpp 440cc8409bb268b3443d8dec85b80dcff4660536fa9663964a5feaec00af06cc ac00772d7788adcc2965d76d71a24ce15b12a0022386aa45088cd2446c0fea57
tests/unit/text_entry_test.cpp 18e4a77ecf14d4ea12483826c00208964ed00af885dce8c452afbd7ff8cd2185 d750a80704d54d6ac68de18b94d7df5e616bc6f080b0203c6beaf3d51561df3b
.clang-format e395beaa072bb3a70b75d4cfbceb11ab920c7bfbee25bac1e65ed926c72b3c18 e395beaa072bb3a70b75d4cfbceb11ab920c7bfbee25bac1e65ed926c72b3c18
.gitignore e897d73a18eed22a2bdf122cdb106da753b2e5d0759f75eb140b009a8641cb44 e897d73a18eed22a2bdf122cdb106da753b2e5d0759f75eb140b009a8641cb44
CMakeLists.txt adc4e2de6e085f4ee14fc7c4cb900c6f7051b287bd871b359fd28216ae5199b2 a839e6380eca2036f8fb30888615299fb924c678b0fc705de2f5e4c9c4abf584
pyproject.toml 4ac3703cc28f121ff2896cc5d5fb4c27af47349f19ea58b3dd30e74d815e33f9 4ac3703cc28f121ff2896cc5d5fb4c27af47349f19ea58b3dd30e74d815e33f9
-->
