# Ported from the Texas Hold'em recomp

Written by `tools/port-from-holdem.py`; do not edit by hand. Every file here entered this
tree as a copy of the file named in the first column, with the identifier rewrites that
script applies. `source` is the SHA-256 of the Hold'em original at the moment of the port;
`ported` is the SHA-256 of this tree's copy immediately after it — so a file whose current
hash differs from `ported` has been edited here, and one whose Hold'em original differs
from `source` has moved on there. A row whose file is absent here was ported and then
deliberately removed; the reason is in the script's `PORTED` list.

`python3 tools/port-from-holdem.py --check` reports both.

Source tree: `../HoldEm`

| file | source SHA-256 | ported SHA-256 |
|---|---|---|
| `tools/survey.py` | `54c2ae55cc63b618…` | `54c2ae55cc63b618…` |
| `tools/funcs.py` | `52eecf9457617d0d…` | `52eecf9457617d0d…` |
| `tools/emit.py` | `3d45d8a3119a6f6e…` | `559c1ada586474e8…` |
| `tools/progress.py` | `797c1c6d8af03480…` | `797c1c6d8af03480…` |
| `tools/manifest.py` | `1fb34d6322976e7e…` | `301be4a4d1261296…` |
| `tools/ppm2png.py` | `4428bbf408271143…` | `4428bbf408271143…` |
| `src/gamedata/install.cpp` | `06a2957c6bc93810…` | `0106dc66dddec5bb…` |
| `src/gamedata/install.h` | `9ecfb7232011f582…` | `53d8c84c0f3171ad…` |
| `src/gamedata/manifest.h` | `a41d611009fb90ab…` | `b9b4534e9162d61a…` |
| `src/gamedata/zip.h` | `499f84fad78f2e96…` | `9a056e48aeda2897…` |
| `src/runtime/arm_runtime.json` | `a483df0cd9059faa…` | `a483df0cd9059faa…` |
| `src/runtime/cpu.h` | `8b7ff7b9ac6cc982…` | `970982544db4a77a…` |
| `src/runtime/eapp_image.cpp` | `3ef1139a21557bb2…` | `f11712d5d4ff6c7c…` |
| `src/runtime/eapp_image.h` | `aa1f2e0c51f09b04…` | `b915497b08acf890…` |
| `src/runtime/main.cpp` | `a8d3809590e13b78…` | `76516d13c63acbf5…` |
| `src/runtime/memory.cpp` | `b30275c82c0f48b0…` | `3b561d2e1bd781a4…` |
| `src/runtime/memory.h` | `b9fc185a3edfc5a5…` | `1a2f850e6573f6be…` |
| `src/runtime/runtime.h` | `640891bbd833907a…` | `75bf1c4f1a1d5eec…` |
| `src/framework/audio.h` | `b6fbfe6e2572df54…` | `baf5911ba90e90f3…` |
| `src/framework/controls.h` | `e67308e032aa5188…` | `fd7b30bf6e9a3beb…` |
| `src/framework/device.h` | `7529187e092839a5…` | `e6e635d6a471dc1e…` |
| `src/framework/graphics.h` | `536638785c6da256…` | `69d4cf520015ee1a…` |
| `src/framework/music_library.h` | `a4161b98cbc9f4c0…` | `be647635376ce827…` |
| `src/framework/storage.h` | `087c38bf2a4d77f1…` | `fc931460bae64f81…` |
| `src/framework/types.h` | `9ff266b1b21ce657…` | `17dd55de3c7e6421…` |
| `src/libeapp/include/ipod_eapp.h` | `d184dca5d6349898…` | `6a0c419d04acbeaf…` |
| `src/libeapp/arm_abi.cpp` | `6ce5ffc2f62a94e8…` | `9ae6066c8c5fc892…` |
| `src/libeapp/async_file.cpp` | `1aa9d3735757af4d…` | `b88e7b5555f5ff52…` |
| `src/libeapp/audio.cpp` | `6ccb98c32605f4ca…` | `85327f4f93d817ea…` |
| `src/libeapp/framework_call.cpp` | `155b17b2a187444e…` | `12fb82270c577a27…` |
| `src/libeapp/heap.h` | `7f6dff0806e521a8…` | `508967032e9f5f9b…` |
| `src/libeapp/host_state.cpp` | `ec509795ed0c537e…` | `9d501fb4c9560ce3…` |
| `src/libeapp/host_state.h` | `4aa0c73426bcb1e2…` | `ccc6ceff0f21f355…` |
| `src/libeapp/imports.json` | `ef7144ea163bb9c3…` | `ef7144ea163bb9c3…` |
| `src/libeapp/input.cpp` | `9853a03973d7844a…` | `4439988949e82281…` |
| `src/libeapp/metadata.cpp` | `3b22836c537e8436…` | `244ac02ae5e1f8a3…` |
| `src/libeapp/misc.cpp` | `05101ccae53ed0ef…` | `23a8d3caf9f18666…` |
| `src/platform/input_bindings.cpp` | `6d564796bc608873…` | `2376556483dbd824…` |
| `src/platform/input_bindings.h` | `e1642aa52bdc28da…` | `4a6da86931a968ea…` |
| `src/platform/paths.cpp` | `29f1feeadf5d4ef8…` | `caabffc28bd21993…` |
| `src/platform/paths.h` | `848ceced8cc86fd8…` | `f5e713e91b9d2f79…` |
| `src/platform/platform.h` | `3669962527585ef9…` | `ea77a898af22c754…` |
| `src/platform/save_store.h` | `4959fe1f713f002a…` | `569782b6cccd115d…` |
| `src/platform/settings.cpp` | `c46d3ce0af58f7de…` | `11400bb4f9b8110d…` |
| `src/platform/settings.h` | `4fade787b78e5acc…` | `dd446c52b2594cf5…` |
| `src/platform/text_entry.h` | `84d956c3a7d7f9d7…` | `e82e01e1e0e14a22…` |
| `src/platform/null/null_platform.cpp` | `1668064d42d1fe6f…` | `6bbe6bc004244457…` |
| `src/platform/sdl3/sdl3_platform.cpp` | `935289d856faf81f…` | `5debd50ead52a26d…` |
| `src/platform/sdl3/music_decoder.h` | `b4afe281265079a5…` | `b88fdd559545f338…` |
| `src/platform/sdl3/macos_settings.h` | `d271d13169fe4c46…` | `4db4f4c08de92034…` |
| `src/platform/sdl3/macos_settings.mm` | `c07edd4d453adcf0…` | `1847322956c5d98c…` |
| `src/platform/sdl3/macos_settings_stub.cpp` | `1b57d497933ef73c…` | `b847c4ceaa735e9b…` |
| `tests/diff.py` | `4a178b48b6083a33…` | `4a178b48b6083a33…` |
| `tests/diff.sh` | `dc1ee138d718a3c0…` | `1abaaf3aa701a6fa…` |
| `tests/frames.py` | `b6395991fb0d22e4…` | `b6395991fb0d22e4…` |
| `tests/frames.sh` | `c2bf337d485c401e…` | `5433743f90744110…` |
| `tests/game-dir.sh` | `f5b8bc44bf93be96…` | `f5b8bc44bf93be96…` |
| `tests/record.sh` | `0d000767f576415e…` | `0d000767f576415e…` |
| `tests/unit/cpu_test.cpp` | `25d66f722daec5e9…` | `82e41f9b6573d07d…` |
| `tests/unit/input_bindings_test.cpp` | `bee6f31e71cd9c8e…` | `89716b5cc6e69e9e…` |
| `tests/unit/install_test.cpp` | `b8c62533e04aae86…` | `8cbb6304cc0578f1…` |
| `tests/unit/render_scale_test.cpp` | `cfd15fc87914afde…` | `a50c4526377c5d51…` |
| `tests/unit/save_files_test.cpp` | `ec1ec190d0543159…` | `d727ca97cf8997c7…` |
| `tests/unit/save_store_test.cpp` | `e72b062bc75baf8e…` | `17113cbdd8e5fa2d…` |
| `tests/unit/settings_test.cpp` | `5c3470cb8c203160…` | `440cc8409bb268b3…` |
| `tests/unit/text_entry_test.cpp` | `578d33a7961eb495…` | `18e4a77ecf14d4ea…` |
| `.clang-format` | `e395beaa072bb3a7…` | `e395beaa072bb3a7…` |
| `.gitignore` | `e897d73a18eed22a…` | `e897d73a18eed22a…` |
| `CMakeLists.txt` | `d20cabc034b78171…` | `a8160e499b7311dc…` |
| `pyproject.toml` | `4ac3703cc28f121f…` | `4ac3703cc28f121f…` |

<!-- full hashes, for the --check command
tools/survey.py 54c2ae55cc63b61830327b94d29fb127bb4c942793656c6286df6e282e8f82a6 54c2ae55cc63b61830327b94d29fb127bb4c942793656c6286df6e282e8f82a6
tools/funcs.py 52eecf9457617d0d2a948dd5f8d4fca231173aebd07da8f1de51a87aa3fce039 52eecf9457617d0d2a948dd5f8d4fca231173aebd07da8f1de51a87aa3fce039
tools/emit.py 3d45d8a3119a6f6ead9d0d1b4d55ebb0c95d062d480adfb2043048033f04e7e1 559c1ada586474e8eba55e8bcaf9cdbebd1fb642c201597ba67ad4962e1ae2d0
tools/progress.py 797c1c6d8af034805ccb150d9a5ce5456fa1eccd6371930d2dc47c03ed31b721 797c1c6d8af034805ccb150d9a5ce5456fa1eccd6371930d2dc47c03ed31b721
tools/manifest.py 1fb34d6322976e7ef21a242ef1517d7d9fb1bb61be19b02bdf19a9b8d3cf79fa 301be4a4d1261296e01269ebcb8a4ed2984a426ca262d1094d6190ee05ddb7e0
tools/ppm2png.py 4428bbf408271143ee9fc8aede3665b2280fe1fa15cafa4baf460362b21d3d0c 4428bbf408271143ee9fc8aede3665b2280fe1fa15cafa4baf460362b21d3d0c
src/gamedata/install.cpp 06a2957c6bc93810561e6f54ccd1d45175eda8054024c001fca7fd4bf3435464 0106dc66dddec5bbb96f045f58919632d629e3e972dabb37bd43d68e299398fa
src/gamedata/install.h 9ecfb7232011f582293081f125264a40d39a5d32fe5582783b0dae6653c828ca 53d8c84c0f3171ad0657a0c4db32ceed2ac66f49b60b285a33d21b0509cae7b7
src/gamedata/manifest.h a41d611009fb90abc488c11be4c34cf6dc3d72e5038d5dd553403455e3f3441c b9b4534e9162d61a59c9cb466f0bbbe7fc1e1f2c22a1a03feb53a1ed1c15ebbe
src/gamedata/zip.h 499f84fad78f2e9650d6bb00b96f985dc273923b58bbb7deaa7ab0131a962025 9a056e48aeda28974bb7833883031407fa9023c11bf441889e71ceed2b046bdf
src/runtime/arm_runtime.json a483df0cd9059faa5892e7d3a4465a636fc8aab3b2d55056ddba05cb36ad78f9 a483df0cd9059faa5892e7d3a4465a636fc8aab3b2d55056ddba05cb36ad78f9
src/runtime/cpu.h 8b7ff7b9ac6cc982ee289715ca3cc0945a83980a4fe6ed77d110097624532382 970982544db4a77ab84e1263658bb02bdff0cbcd766b261725ffcfe49af8e927
src/runtime/eapp_image.cpp 3ef1139a21557bb29d24b86b9e9728af21f0c72a32bcb1cae550ce49626d613b f11712d5d4ff6c7ce2a818e8764c302ec806890a38bfd0861cfc3d4ca239e15f
src/runtime/eapp_image.h aa1f2e0c51f09b04fb2f52fea8bae42132edc5a02b9a21eaad888865c335f03c b915497b08acf890972b269e1fb1a47b6d84fa4cc43ba9b0f9db1f7a23d2c56d
src/runtime/main.cpp a8d3809590e13b784651bbb0c06202462c0756d72bd294119cfe0824adaec05f 76516d13c63acbf58dcac19a33edd3611bb117eecad0658d49bbf87f2bea6091
src/runtime/memory.cpp b30275c82c0f48b0749e00cd795f691ad0ede7cf00ac09f517a59780545b80d4 3b561d2e1bd781a45ad9e240ae26972d6ab5a08d7f05b8b04bc25e173bf68440
src/runtime/memory.h b9fc185a3edfc5a550668e2cbbae33b28ad3101def96904177a79a7469fbfcdc 1a2f850e6573f6be3112410927613b0dfe652604c4d91b47905a1d56c19c83a3
src/runtime/runtime.h 640891bbd833907aabddfbc61a657d9bd15b385e9e4021434190ce6d08a075d1 75bf1c4f1a1d5eec0a853472931c2ed50f7b0a91c7fc0fe82a1324e08727c950
src/framework/audio.h b6fbfe6e2572df54ddc16c01fa5a3d5e365171df3b4dfedd37648123c8a308b1 baf5911ba90e90f3a78b6220e08bd58db980ffe127f5cb0a9c403105615dc55a
src/framework/controls.h e67308e032aa51887ae81769c69284b723c0470ce8fecb19a4933e32a7bdfac3 fd7b30bf6e9a3bebcdbf3d7c723405b69e7fb94d47854c05560b248bc28630fa
src/framework/device.h 7529187e092839a5fecca545b27863040087fd19aad312c0111aeb2d8dc64b31 e6e635d6a471dc1ea1edfd6a3cfd2e1c17b1b1c6d3531c0fba7bde65674b521b
src/framework/graphics.h 536638785c6da256788a3033bc39d553494c423a96f17100f90b705e53ec1888 69d4cf520015ee1a54f7e913f09a894c0542a3e9a9ff94132230e6c8f4192810
src/framework/music_library.h a4161b98cbc9f4c0d94562eef089a89e23516213db7aa61b6ec1ba7d8c9b6d81 be647635376ce827b5e75a69f00742e5c1ca33ccbe5e88b3264485ec03233eb7
src/framework/storage.h 087c38bf2a4d77f1a1462cd626460f6879c851771bfb6183fd643d2ad51cf5a6 fc931460bae64f816816d03a40a017e4ecac9de297f19c28682a477a3850a0ad
src/framework/types.h 9ff266b1b21ce657ef6ec5c890142a58f2cca6383157190e545b0fc638e1d58c 17dd55de3c7e642172430fd821e673e38cf853708ef1ad7f6a48cb8107d31241
src/libeapp/include/ipod_eapp.h d184dca5d63498987c160f67b315835d1f63d219f3c6c0be296ad7dcb8367db7 6a0c419d04acbeaf1c3df86ab72a82e798695667fdc6d9ad60cf0cb860f7074d
src/libeapp/arm_abi.cpp 6ce5ffc2f62a94e8457e02045c5d8a270e9540aa9000aa288b45d7443d8e52a8 9ae6066c8c5fc892a4ade9260c7e7bf8df32a764ad60cf313a22b4ca5656561d
src/libeapp/async_file.cpp 1aa9d3735757af4db5a28cf3d0f0827dd08a0516fd4baab5872cb46feafea6f6 b88e7b5555f5ff52ea8ffe997144d4e2cceee77ebd86f38262eba4c8639ebbf5
src/libeapp/audio.cpp 6ccb98c32605f4caa3171e5fbc95446203eb08b09f628b27cb2099f22bcfe8bc 85327f4f93d817ea54629b1704bc0bb26aaadb31f1afc5c6f1b582778866575e
src/libeapp/framework_call.cpp 155b17b2a187444e80f90685e4636bbfa6780954f3b00428b2dac3992f7ef98d 12fb82270c577a276ab14ce84bb099c670892da7e455192bb95002dc3e17d2b8
src/libeapp/heap.h 7f6dff0806e521a8177687d364144dbcaaea0d91e661e1f15e8d9c910a5d9ee2 508967032e9f5f9bd9cfe63bddbd530d60f88ff0a3264a3bb35e9b3a97dc6511
src/libeapp/host_state.cpp ec509795ed0c537e99442bb32509228a4e319e7375986c012abb465eced74e87 9d501fb4c9560ce38d0a4a0c09b94cab233eb4ad8e194cc39e3f864f23328a55
src/libeapp/host_state.h 4aa0c73426bcb1e2acbd37ec058ccf94618c1541a177b8fa54081153ab602a21 ccc6ceff0f21f355114221935a66d13582cca877c3aa45d8baafa17a5583e388
src/libeapp/imports.json ef7144ea163bb9c35ad3ed88a1d64ec6c318ba9bae16e31d43f6622268810293 ef7144ea163bb9c35ad3ed88a1d64ec6c318ba9bae16e31d43f6622268810293
src/libeapp/input.cpp 9853a03973d7844a5cfbb058d3c2781de4780a1c10b5644382ecec55fb278021 4439988949e82281d54232406f7b351dbfd9716924332f8eae66aae1f366f7cf
src/libeapp/metadata.cpp 3b22836c537e84364f002a1a2a6aa0238f1037be5a110d9bff18af531ffb5493 244ac02ae5e1f8a3aae3aac16c3d2f7aace4998562019d76e01c57edbdd6da36
src/libeapp/misc.cpp 05101ccae53ed0ef264aba2a3fb4d468b3fa027d991569a5500bba9e91514783 23a8d3caf9f186665de840c65fbe08b2206f20221ad57c6a0bb2d3d86bddf8cb
src/platform/input_bindings.cpp 6d564796bc608873cb821a8c9c92c66f06ff18e7f810429f482350d987880ec0 2376556483dbd824d11613e02366fbde76c6a22bd363148c32a20c01653cad33
src/platform/input_bindings.h e1642aa52bdc28da72a3c732537742c4d08663982cd0f8fa990ccafeb81f01aa 4a6da86931a968eacd36dd7988ce8e0ab7660711967d524cc11ab3dbe2128ae5
src/platform/paths.cpp 29f1feeadf5d4ef8a90fefded3dc8866c75c7bcefa1696b47fa556aaa4f75c85 caabffc28bd21993ff0e1008a7fb334b8418f83a77623b0939ba31f9534bc6e1
src/platform/paths.h 848ceced8cc86fd8d489a9b4c8c30c4f745a8ccc5c7e7c9bd79966f7bf7c45c6 f5e713e91b9d2f7920642e6d603db5ac104055a09a8e7d41b59f27cc62d09839
src/platform/platform.h 3669962527585ef972463248d9c51e4cc33981dd53cf3bb095d3b768d0e79b84 ea77a898af22c754ab168a6e1588a361981f8f4863dc7f1d5590e0b04bb3c4e3
src/platform/save_store.h 4959fe1f713f002a2c6c788cdb488000e6a58ff685ba5fe1bb9f0736c764f87a 569782b6cccd115dcc19c711a0dbe8ec9c80990dd4c8e6e3be0793278fb4f465
src/platform/settings.cpp c46d3ce0af58f7de04c7f54dfb3303b498f054df68a63ac39bef1017b1eb5cd5 11400bb4f9b8110d00aebc491a6429ed25e09a4fc03c7854da779fdfa97e30e8
src/platform/settings.h 4fade787b78e5acc3f4acfbe60c792212753e22e829bf372f8417743737a1f65 dd446c52b2594cf5d37e1bc87e3bb865a7483a7111513d76901875978b88aba8
src/platform/text_entry.h 84d956c3a7d7f9d737a93f35cc5758321118bfa740326d47b6e6ac5e8ebb7353 e82e01e1e0e14a223aa13af1009dbce38ce0bb5326f39776a70103c4027a3e8c
src/platform/null/null_platform.cpp 1668064d42d1fe6fece62b49ca0740f300626c1e0a515a28af524b6f8507bd49 6bbe6bc0042444575b3b144d8cdb169c263acf004e008d1842be68eb31514d0b
src/platform/sdl3/sdl3_platform.cpp 935289d856faf81feabff88dbcbe112812aad5567da8d2137ab286f01d11b576 5debd50ead52a26dc30002413258b024920f2b5d1064aff4eb15e2b37ef1bc48
src/platform/sdl3/music_decoder.h b4afe281265079a5d10ca5d861daf87f68ccf7cdf2c0c8aefca9b278a125e9e5 b88fdd559545f3382edfc1914e841551f107a7b1d12fde8290efa615f4f44c34
src/platform/sdl3/macos_settings.h d271d13169fe4c46d3ad8a32ad07b21f21648a8e8bcfe2e05024e1c13de92ece 4db4f4c08de92034b987b80d44e250e9f342c3b40ce7976251e42921592dd6c4
src/platform/sdl3/macos_settings.mm c07edd4d453adcf0b9dcc80967a4c199ce4174575d9b48a97e512a3de1f0ed53 1847322956c5d98cf45041b3a6855abefd8d8c90ef9d644ecf84ac1ee0c69f89
src/platform/sdl3/macos_settings_stub.cpp 1b57d497933ef73ccba91aa2bb2ad06614f904eb4760bd368e7b52d20325a94c b847c4ceaa735e9b6a95a979481e418450dcdd8f8d81c1b62b4b9d4395aa2d4d
tests/diff.py 4a178b48b6083a337daa0f7bdb0737070dcd292979ef583dcb0519e6eb379dd9 4a178b48b6083a337daa0f7bdb0737070dcd292979ef583dcb0519e6eb379dd9
tests/diff.sh dc1ee138d718a3c0cd330e4fe960898d4eb29e9588efd21b484108da5dbad453 1abaaf3aa701a6fa119404ef11de3e30b873268b597023c32c2ae14a0dfccda1
tests/frames.py b6395991fb0d22e405a1b0ec9c78a6f57f53d87bb7fd9c1db878b0ad2205dec6 b6395991fb0d22e405a1b0ec9c78a6f57f53d87bb7fd9c1db878b0ad2205dec6
tests/frames.sh c2bf337d485c401e72f5d8b121b3b3c5ca1d86cc5f8a8c0372378a7dc21a42e6 5433743f907441102e2a1ccbdc50de8e094eebe34eca8f136050ef7e0d127b83
tests/game-dir.sh f5b8bc44bf93be9647261da78f10b4132e93b3870dcf18a6a6c623f4c79418b8 f5b8bc44bf93be9647261da78f10b4132e93b3870dcf18a6a6c623f4c79418b8
tests/record.sh 0d000767f576415e00a0b3e13885e4d2c7d0d202f10bda65f1614f3fb3851a31 0d000767f576415e00a0b3e13885e4d2c7d0d202f10bda65f1614f3fb3851a31
tests/unit/cpu_test.cpp 25d66f722daec5e992f481fdcdb49b639360087c1fbfe9e8ecab6094f6dc7ad1 82e41f9b6573d07ddd8701a8a727a4184d1f138fd2aa7c0d2adc2ce870b22cbf
tests/unit/input_bindings_test.cpp bee6f31e71cd9c8e8a4ea263d1e880b01023053e75c0be871c13001b7a0871b7 89716b5cc6e69e9efddf9cae0de51bcb553879325f1ddd03fa0d3d11949341da
tests/unit/install_test.cpp b8c62533e04aae86b6f2795ebbf9d6fea924ee5206255fb2bab369114e08c950 8cbb6304cc0578f10511de884dad508bc866d0f20937cf2373d115864de4691e
tests/unit/render_scale_test.cpp cfd15fc87914afdec31a7c524633be7e46b88796fe64dd6b33bbe80e9898691d a50c4526377c5d518767debfe17ab166276c01aed58ceab6fa520d7fd878db66
tests/unit/save_files_test.cpp ec1ec190d05431593977e0a8b42c024b8dd551a444fd3946f578c949ea252898 d727ca97cf8997c7fd9295d02e10e6109e5541c71234d975cb820081d9429edd
tests/unit/save_store_test.cpp e72b062bc75baf8ea30e49fb77222ff9faf54dd2f62ceadb4ac4ca0136919557 17113cbdd8e5fa2de27a52d8886ba4177d6a11c7587a8eb1b32654eed354b97c
tests/unit/settings_test.cpp 5c3470cb8c2031605ca46b57c22408ee5619f8cc2a24438a9ec44a25dcc50cd0 440cc8409bb268b3443d8dec85b80dcff4660536fa9663964a5feaec00af06cc
tests/unit/text_entry_test.cpp 578d33a7961eb49559cb321b535d1fa8c3557133878d3f7224f3d62f45c09bc3 18e4a77ecf14d4ea12483826c00208964ed00af885dce8c452afbd7ff8cd2185
.clang-format e395beaa072bb3a70b75d4cfbceb11ab920c7bfbee25bac1e65ed926c72b3c18 e395beaa072bb3a70b75d4cfbceb11ab920c7bfbee25bac1e65ed926c72b3c18
.gitignore e897d73a18eed22a2bdf122cdb106da753b2e5d0759f75eb140b009a8641cb44 e897d73a18eed22a2bdf122cdb106da753b2e5d0759f75eb140b009a8641cb44
CMakeLists.txt d20cabc034b7817103f1d4515a7b0d6773932d16ef38e689870090f8064f98a9 a8160e499b7311dcc5bfceb0c907f79c3d7a5610d99164c81359454c78227f27
pyproject.toml 4ac3703cc28f121ff2896cc5d5fb4c27af47349f19ea58b3dd30e74d815e33f9 4ac3703cc28f121ff2896cc5d5fb4c27af47349f19ea58b3dd30e74d815e33f9
-->
