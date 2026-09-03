# Ported from the Lost recomp

Written by `tools/port-from-lost.py`; do not edit by hand. Every file here entered this
tree as a copy of the file named in the first column, with the identifier rewrites that
script applies. `source` is the SHA-256 of the Lost original at the moment of the port;
`ported` is the SHA-256 of this tree's copy immediately after it — so a file whose current
hash differs from `ported` has been edited here, and one whose Lost original differs from
`source` has moved on there. A row whose file is absent here was ported and then
deliberately removed; the reason is in the script's `PORTED` list.

`python3 tools/port-from-lost.py --check` reports both.

Source tree: `../Lost`

| file | source SHA-256 | ported SHA-256 |
|---|---|---|
| `tools/survey.py` | `38595d51e571e886…` | `38595d51e571e886…` |
| `tools/funcs.py` | `90a6116b06adf580…` | `90a6116b06adf580…` |
| `tools/emit.py` | `827ff584c5062292…` | `3d45d8a3119a6f6e…` |
| `tools/progress.py` | `9c38d39ee99152b6…` | `9c38d39ee99152b6…` |
| `tools/manifest.py` | `1cead57043c4472d…` | `e9b91d7cfb009b88…` |
| `tools/ppm2png.py` | `4428bbf408271143…` | `4428bbf408271143…` |
| `src/gamedata/install.cpp` | `7353a7133133565a…` | `e9f7f1ad3c79ebc8…` |
| `src/gamedata/install.h` | `66debeee596b807e…` | `dd74ffa561bd5562…` |
| `src/gamedata/manifest.h` | `680713334eb4add4…` | `4fc455de92e54a15…` |
| `src/gamedata/zip.cpp` | `06f80bc8379ce3d6…` | `014351a32702c8e5…` |
| `src/gamedata/zip.h` | `a121bc12b5606436…` | `8c9c87bfb6429565…` |
| `src/runtime/arm_runtime.json` | `a483df0cd9059faa…` | `a483df0cd9059faa…` |
| `src/runtime/cpu.h` | `e36a30b43bdd3b58…` | `60c24a54ecad722b…` |
| `src/runtime/eapp_image.cpp` | `ac7c432f7b0069f1…` | `3ef1139a21557bb2…` |
| `src/runtime/eapp_image.h` | `2f3dc0252e0e6935…` | `aa1f2e0c51f09b04…` |
| `src/runtime/main.cpp` | `2d916750f1b46caa…` | `eb2885878296b47c…` |
| `src/runtime/memory.cpp` | `1f699e07b2561678…` | `6189b0ca52760966…` |
| `src/runtime/memory.h` | `259c4923d95ed51d…` | `27487d74e95a493a…` |
| `src/runtime/runtime.cpp` | `8a4fb97b88069419…` | `cb2345dd2fb31f6f…` |
| `src/runtime/runtime.h` | `185a9882301ce4d6…` | `68b56a409b095465…` |
| `src/framework/audio.h` | `590bcf6ce79fe602…` | `b6fbfe6e2572df54…` |
| `src/framework/controls.h` | `5bb34049a5b6912c…` | `e67308e032aa5188…` |
| `src/framework/device.h` | `f04a4f606bfa1be5…` | `7529187e092839a5…` |
| `src/framework/graphics.h` | `3445332180df7c0f…` | `08b19e8808cd6f4c…` |
| `src/framework/music_library.h` | `484ab670f802ddbe…` | `a4161b98cbc9f4c0…` |
| `src/framework/storage.h` | `1c4d41678a1e2d6d…` | `8c88a7ee3da79557…` |
| `src/framework/types.h` | `c7114ecbf8b6b80f…` | `b9a108d0ca145d8a…` |
| `src/libeapp/include/ipod_eapp.h` | `555e354509143b13…` | `93c555d87789cff0…` |
| `src/libeapp/arm_abi.cpp` | `49f8b446c1448a80…` | `795cc8d1b06f727e…` |
| `src/libeapp/async_file.cpp` | `d0cd1836fccc0d74…` | `4bbb77f76455e4f3…` |
| `src/libeapp/audio.cpp` | `7034b57cdf6e0c28…` | `6ccb98c32605f4ca…` |
| `src/libeapp/framework_call.cpp` | `223ff937de3ce6bf…` | `155b17b2a187444e…` |
| `src/libeapp/heap.cpp` | `be36890a659c03d0…` | `cbe891420e9e1661…` |
| `src/libeapp/heap.h` | `66d1fa2fdf1d7ea2…` | `c8368d7cb89ce570…` |
| `src/libeapp/host_state.cpp` | `0b8f8788ee6d4761…` | `ec509795ed0c537e…` |
| `src/libeapp/host_state.h` | `e1e214f6abb61ada…` | `4aa0c73426bcb1e2…` |
| `src/libeapp/imports.json` | `4f9c51a85ba4d99d…` | `4f9c51a85ba4d99d…` |
| `src/libeapp/input.cpp` | `2fdc6017ad3b71e4…` | `9853a03973d7844a…` |
| `src/libeapp/metadata.cpp` | `b6cd4322c2fdcc10…` | `3b22836c537e8436…` |
| `src/libeapp/misc.cpp` | `45f39852a23801bb…` | `05101ccae53ed0ef…` |
| `src/platform/input_bindings.cpp` | `9a71b1d14baf9b6f…` | `6d564796bc608873…` |
| `src/platform/input_bindings.h` | `9152968be9f5fd51…` | `e1642aa52bdc28da…` |
| `src/platform/paths.cpp` | `dea39ca559fce97e…` | `29f1feeadf5d4ef8…` |
| `src/platform/paths.h` | `6bb822371ca00bb2…` | `848ceced8cc86fd8…` |
| `src/platform/platform.h` | `ddd6647b6cfd582a…` | `3669962527585ef9…` |
| `src/platform/save_store.h` | `ef53850fb3dbf7d2…` | `c7e4f5f1d1754b8d…` |
| `src/platform/settings.cpp` | `0c688ed78c114be9…` | `c46d3ce0af58f7de…` |
| `src/platform/settings.h` | `b085cb0e544a3be6…` | `4fade787b78e5acc…` |
| `src/platform/text_entry.h` | `8b1b1dbeecbec9c4…` | `87139875aa54e713…` |
| `src/platform/null/null_platform.cpp` | `d44d22a372ca5ddf…` | `1668064d42d1fe6f…` |
| `src/platform/sdl3/sdl3_platform.cpp` | `7f699cbda74e8178…` | `b334fe641bc6d1c6…` |
| `src/platform/sdl3/music_decoder.h` | `638f75a04c43c4da…` | `b4afe281265079a5…` |
| `src/platform/sdl3/macos_settings.h` | `9f2b7875ce77a960…` | `d271d13169fe4c46…` |
| `src/platform/sdl3/macos_settings.mm` | `27214a146794cd16…` | `c07edd4d453adcf0…` |
| `src/platform/sdl3/macos_settings_stub.cpp` | `66d03e49f09573ef…` | `1b57d497933ef73c…` |
| `tests/diff.py` | `4a178b48b6083a33…` | `4a178b48b6083a33…` |
| `tests/diff.sh` | `e24e1b3e9d0b46c2…` | `dc1ee138d718a3c0…` |
| `tests/frames.py` | `b6395991fb0d22e4…` | `b6395991fb0d22e4…` |
| `tests/frames.sh` | `58e63a83ce965ba7…` | `c2bf337d485c401e…` |
| `tests/game-dir.sh` | `5a192e7450b3551d…` | `5a192e7450b3551d…` |
| `tests/record.sh` | `7f6cf20041bc7a3c…` | `7f6cf20041bc7a3c…` |
| `tests/unit/cpu_test.cpp` | `8d52b8809901a67e…` | `007d0b1ef5102b4a…` |
| `tests/unit/input_bindings_test.cpp` | `ecef7f3fab80a693…` | `bee6f31e71cd9c8e…` |
| `tests/unit/install_test.cpp` | `59a2e38a2fae6bc6…` | `6ba23eaa490aea9f…` |
| `tests/unit/render_scale_test.cpp` | `c7429fd4f00b7037…` | `cb7c8a46104083e1…` |
| `tests/unit/save_files_test.cpp` | `8deafd296768cd05…` | `ec1ec190d0543159…` |
| `tests/unit/save_store_test.cpp` | `f323110b7b41a63a…` | `e72b062bc75baf8e…` |
| `tests/unit/settings_test.cpp` | `bc9f465c3a317f78…` | `5c3470cb8c203160…` |
| `tests/unit/text_entry_test.cpp` | `256272cd068a76e6…` | `578d33a7961eb495…` |
| `.clang-format` | `e395beaa072bb3a7…` | `e395beaa072bb3a7…` |
| `.gitignore` | `e897d73a18eed22a…` | `e897d73a18eed22a…` |
| `CMakeLists.txt` | `172b5782eae0bdda…` | `45e05356cf656147…` |
| `pyproject.toml` | `4ac3703cc28f121f…` | `4ac3703cc28f121f…` |

<!-- full hashes, for the --check command
tools/survey.py 38595d51e571e886efa2eddcb42ddbe83472870ee4a951aa49564ea07faf8f9c 38595d51e571e886efa2eddcb42ddbe83472870ee4a951aa49564ea07faf8f9c
tools/funcs.py 90a6116b06adf5806b5c48427e6f2d9fd3271afd594360aa07ee51e33df4d286 90a6116b06adf5806b5c48427e6f2d9fd3271afd594360aa07ee51e33df4d286
tools/emit.py 827ff584c5062292632bb89308071ddc002fe9e9b45a16541669a4cd6d63c302 3d45d8a3119a6f6ead9d0d1b4d55ebb0c95d062d480adfb2043048033f04e7e1
tools/progress.py 9c38d39ee99152b6f46508d9bd0d06246be75ad49ebe6d79d788817f124256fe 9c38d39ee99152b6f46508d9bd0d06246be75ad49ebe6d79d788817f124256fe
tools/manifest.py 1cead57043c4472d7a1fa5cc0d75a7a8a76f9a6313b56eca009e1cbb77ccd0a9 e9b91d7cfb009b889fff0b99a2bcad37fb7d7ff717da4e959287703a6485dd21
tools/ppm2png.py 4428bbf408271143ee9fc8aede3665b2280fe1fa15cafa4baf460362b21d3d0c 4428bbf408271143ee9fc8aede3665b2280fe1fa15cafa4baf460362b21d3d0c
src/gamedata/install.cpp 7353a7133133565a377e955436e51643b034cc00c604423588f024d2e68c1769 e9f7f1ad3c79ebc8f81c036f5d9c84c9be416307b812c61fa4d007c9dfd0a934
src/gamedata/install.h 66debeee596b807e62ab583b0ab29a1ae8028ce57c312d86a0c596d071b90bff dd74ffa561bd5562bca8936f606c77b8ea586caa1cc5b4792b419b73dacb1522
src/gamedata/manifest.h 680713334eb4add4a1f94c5864cb7aaea0c93455637f4317a0da5b23c8fc901c 4fc455de92e54a15e1d4c5f4f5bcae5bb481fc02c3d20486acb5ffe0e279105e
src/gamedata/zip.cpp 06f80bc8379ce3d6d1cab38ab42739423b90a7f333b8f30b99506b92965981ac 014351a32702c8e5230172afff2cb6f1f6f366d7c40f2f6c9b2f5c5eadaac0d4
src/gamedata/zip.h a121bc12b56064362960f5ce646d41b705e0344ce41ac4113d7bf743673c150c 8c9c87bfb64295651a3a0bc45835032f03b8389f07de088f35458e2b35cf6949
src/runtime/arm_runtime.json a483df0cd9059faa5892e7d3a4465a636fc8aab3b2d55056ddba05cb36ad78f9 a483df0cd9059faa5892e7d3a4465a636fc8aab3b2d55056ddba05cb36ad78f9
src/runtime/cpu.h e36a30b43bdd3b58a2397fa12805d211a0e84bf631429c5c50ddca5df00a31ee 60c24a54ecad722ba886660025e15ab2e3f9ee356fca09d6d0294108534203ac
src/runtime/eapp_image.cpp ac7c432f7b0069f11b7188e406f526d5076b6d0fc1f3fd3fd886f55e332e951c 3ef1139a21557bb29d24b86b9e9728af21f0c72a32bcb1cae550ce49626d613b
src/runtime/eapp_image.h 2f3dc0252e0e69350a760f0fa95f9469bc899e048a50fbad6ea11d2b7190e520 aa1f2e0c51f09b04fb2f52fea8bae42132edc5a02b9a21eaad888865c335f03c
src/runtime/main.cpp 2d916750f1b46caa4cdcb9806b069e5a6e096e37d81c735b4611ffebd716dc26 eb2885878296b47c4a2a8a31cc01f8d692978009cfdd435d4d7605d103e37b1d
src/runtime/memory.cpp 1f699e07b256167818cbadda670b8c3e911f8e21296513f0351be77384b4285f 6189b0ca52760966aeed767b83494d33e8b3655c295275269a8ce6735ae583ee
src/runtime/memory.h 259c4923d95ed51d23966f76511019cd50f64cb3abc50a9f7ea8d688ff7bb8b2 27487d74e95a493afa80be9c13759b69bbb85ad29d6c8f7251002d8cc3d459c4
src/runtime/runtime.cpp 8a4fb97b88069419e54e84293a9d21cf1eb25463ef86e11546a7e87cd4b5077d cb2345dd2fb31f6f0183349d1361d29007cbf8a617044f0fbd6ee30289b54ab9
src/runtime/runtime.h 185a9882301ce4d6f59f41edbb3b1cf2d92972f472977369e8542259c3467b5c 68b56a409b0954650d18d52d3d7583f00d951cbe3f2b14b2e2ad44f3e9647cd2
src/framework/audio.h 590bcf6ce79fe602595ad35c43fc08e845cde48d84ae27c226ea60308c0c4967 b6fbfe6e2572df54ddc16c01fa5a3d5e365171df3b4dfedd37648123c8a308b1
src/framework/controls.h 5bb34049a5b6912c593fe06d8e8693386d3b83f510a3b212dbaec28c1f643a8f e67308e032aa51887ae81769c69284b723c0470ce8fecb19a4933e32a7bdfac3
src/framework/device.h f04a4f606bfa1be528352615a745d6c3c523165f74e3d5480da3e9c6545b6399 7529187e092839a5fecca545b27863040087fd19aad312c0111aeb2d8dc64b31
src/framework/graphics.h 3445332180df7c0fe024a51ceb0b9fa118d72fdea916772e819f97edb6b68790 08b19e8808cd6f4c35c17127fefd176167e1cf29570b5994744684d1653fadce
src/framework/music_library.h 484ab670f802ddbe0ce6a066cb2c1a96f643098274a6e08f50500482954cd34d a4161b98cbc9f4c0d94562eef089a89e23516213db7aa61b6ec1ba7d8c9b6d81
src/framework/storage.h 1c4d41678a1e2d6d49e12c478eca1c34324f72ab081f974f758d378e82a14e62 8c88a7ee3da79557a56001836bd46c808cd360cb2e695d2fd0f3c16a98a87a5d
src/framework/types.h c7114ecbf8b6b80fd0480025955240fb78dda4961ff2765f7d73d50299e174bc b9a108d0ca145d8a1e5f551352a857b9afe7c04f97757a97a5ccaf0ca49fe8d9
src/libeapp/include/ipod_eapp.h 555e354509143b13fa32b1a9f4145ddd38379b5ee3bdb6c3e40b50771260578c 93c555d87789cff0accb51c750b7913f45a3a9a87d58ddcfde4e1d43600f1898
src/libeapp/arm_abi.cpp 49f8b446c1448a8066c56a05bafb4627facfec7911fa451f6842e8e6cf72ba6b 795cc8d1b06f727eef1874cfe6ba71e9d22681fcf73c30299b26a6e42ff158da
src/libeapp/async_file.cpp d0cd1836fccc0d74e2d808cc68b660fec5e1d14f8026bfa0a50ef6c99c829ac2 4bbb77f76455e4f310bc0b46c2071d10c31f75456812d4ddb0e4d280d5f9441f
src/libeapp/audio.cpp 7034b57cdf6e0c2814efd826a42b4779cb684e603d1caa85115d6a9ccde07d7c 6ccb98c32605f4caa3171e5fbc95446203eb08b09f628b27cb2099f22bcfe8bc
src/libeapp/framework_call.cpp 223ff937de3ce6bfee7145c6306205f25a9bea24a6ed9a8fed784ade21d73944 155b17b2a187444e80f90685e4636bbfa6780954f3b00428b2dac3992f7ef98d
src/libeapp/heap.cpp be36890a659c03d0dfad939ece5d9439528e37c0fcca42965a2ae3681d8bf444 cbe891420e9e16618fa8d50ef12a3f7b3939be5da1ec327664394d5fb123afbe
src/libeapp/heap.h 66d1fa2fdf1d7ea2ab0b3272a0067eae67045fb8829a289d943083bf7e90357c c8368d7cb89ce57094d3dffca00767b358d7500ed9bccd89f0058295f4c0ba95
src/libeapp/host_state.cpp 0b8f8788ee6d47611a81b76c83007c1878bc1ac09ae9dedbd95a4d45c6ed6ad7 ec509795ed0c537e99442bb32509228a4e319e7375986c012abb465eced74e87
src/libeapp/host_state.h e1e214f6abb61adacadee689e2f98899ce219472c80d6a732d58ca92315f0885 4aa0c73426bcb1e2acbd37ec058ccf94618c1541a177b8fa54081153ab602a21
src/libeapp/imports.json 4f9c51a85ba4d99d5f3503e5e84f5ebd031aa88195fac4eefbaebc0135ea8e8c 4f9c51a85ba4d99d5f3503e5e84f5ebd031aa88195fac4eefbaebc0135ea8e8c
src/libeapp/input.cpp 2fdc6017ad3b71e4fad7d0edb5743005360804c0b6fe29d8c1b916ff3c24bc58 9853a03973d7844a5cfbb058d3c2781de4780a1c10b5644382ecec55fb278021
src/libeapp/metadata.cpp b6cd4322c2fdcc10a555171fd719ac331dc154a8fcf1b659a8d858a703e614bf 3b22836c537e84364f002a1a2a6aa0238f1037be5a110d9bff18af531ffb5493
src/libeapp/misc.cpp 45f39852a23801bbd71aa074203378cd1a62ebd674a044a7c9bdb768738566bb 05101ccae53ed0ef264aba2a3fb4d468b3fa027d991569a5500bba9e91514783
src/platform/input_bindings.cpp 9a71b1d14baf9b6f94e553616bbc08edc34184fea6da8cfac1e311ed961485fe 6d564796bc608873cb821a8c9c92c66f06ff18e7f810429f482350d987880ec0
src/platform/input_bindings.h 9152968be9f5fd51e6e00e5271d0ca37501917023dd799958b4cefe2451eb9ba e1642aa52bdc28da72a3c732537742c4d08663982cd0f8fa990ccafeb81f01aa
src/platform/paths.cpp dea39ca559fce97ef6b175908560b0ea3d37f60729eab56d235bc566d256ca00 29f1feeadf5d4ef8a90fefded3dc8866c75c7bcefa1696b47fa556aaa4f75c85
src/platform/paths.h 6bb822371ca00bb2330c9cc681ab0411b984b1db68f8c1b4fada94b233525d9e 848ceced8cc86fd8d489a9b4c8c30c4f745a8ccc5c7e7c9bd79966f7bf7c45c6
src/platform/platform.h ddd6647b6cfd582a61f3e3dbee454cc248a42c960dd15fde177ca9b982e6a98d 3669962527585ef972463248d9c51e4cc33981dd53cf3bb095d3b768d0e79b84
src/platform/save_store.h ef53850fb3dbf7d26eb3f6e4e7e03f07fff2f0751cf759889b07cfc02fc7c4bf c7e4f5f1d1754b8df86f2fe99d946f3bd106fa143b0bf56c8e47fa59c1b47150
src/platform/settings.cpp 0c688ed78c114be983389cc10dc288a2eefae9899c6a98f76b59b6de5bd81141 c46d3ce0af58f7de04c7f54dfb3303b498f054df68a63ac39bef1017b1eb5cd5
src/platform/settings.h b085cb0e544a3be66ff5dd5d86089fe9b5b903d0bf129f6615dd83a5ab49e677 4fade787b78e5acc3f4acfbe60c792212753e22e829bf372f8417743737a1f65
src/platform/text_entry.h 8b1b1dbeecbec9c4c7e42dbb65253de34d8647a0504ec70c4cb02367dc2b13b0 87139875aa54e7135f7f91b4508773eab92361c491daa29c63b20314650f5393
src/platform/null/null_platform.cpp d44d22a372ca5ddfe497209ceadb14cf629e48d3757a9750d7987810ddd80b37 1668064d42d1fe6fece62b49ca0740f300626c1e0a515a28af524b6f8507bd49
src/platform/sdl3/sdl3_platform.cpp 7f699cbda74e817800987c4dc16897905c8a307a593b2fbc338ff92a111e2d56 b334fe641bc6d1c6ac70c542d1986c717edcfca615d0f2e6eac0102adf2f7720
src/platform/sdl3/music_decoder.h 638f75a04c43c4dae5cb9a81166ce7ac67ac631defda7121af71d3a7cab233af b4afe281265079a5d10ca5d861daf87f68ccf7cdf2c0c8aefca9b278a125e9e5
src/platform/sdl3/macos_settings.h 9f2b7875ce77a960a36b57775f3fa25f92caaff7205396e9368025763172c076 d271d13169fe4c46d3ad8a32ad07b21f21648a8e8bcfe2e05024e1c13de92ece
src/platform/sdl3/macos_settings.mm 27214a146794cd16f513e015e2699a9f962070f4725b410b674774d0d0e1ac1e c07edd4d453adcf0b9dcc80967a4c199ce4174575d9b48a97e512a3de1f0ed53
src/platform/sdl3/macos_settings_stub.cpp 66d03e49f09573ef027024140b5a93cbac16c35b558ce375d1d00ea71e6e282e 1b57d497933ef73ccba91aa2bb2ad06614f904eb4760bd368e7b52d20325a94c
tests/diff.py 4a178b48b6083a337daa0f7bdb0737070dcd292979ef583dcb0519e6eb379dd9 4a178b48b6083a337daa0f7bdb0737070dcd292979ef583dcb0519e6eb379dd9
tests/diff.sh e24e1b3e9d0b46c26ca2ec44da36952f2399279522edbf93447e51220c7c5a35 dc1ee138d718a3c0cd330e4fe960898d4eb29e9588efd21b484108da5dbad453
tests/frames.py b6395991fb0d22e405a1b0ec9c78a6f57f53d87bb7fd9c1db878b0ad2205dec6 b6395991fb0d22e405a1b0ec9c78a6f57f53d87bb7fd9c1db878b0ad2205dec6
tests/frames.sh 58e63a83ce965ba71b6fec8e3f0c3299b4d1c2a8ddbcd5fc07937f330997aa95 c2bf337d485c401e72f5d8b121b3b3c5ca1d86cc5f8a8c0372378a7dc21a42e6
tests/game-dir.sh 5a192e7450b3551d9b6606efbf952b7700c7ca67a1006bcd52e17472d1a61a65 5a192e7450b3551d9b6606efbf952b7700c7ca67a1006bcd52e17472d1a61a65
tests/record.sh 7f6cf20041bc7a3c0bd2f6f23fdb72bfd3a9e173078ddcb1fa9f330f4b46e180 7f6cf20041bc7a3c0bd2f6f23fdb72bfd3a9e173078ddcb1fa9f330f4b46e180
tests/unit/cpu_test.cpp 8d52b8809901a67e99c0923e4f990faa664b75475ef7eeaa42870a285af11075 007d0b1ef5102b4a89999a0a1e531c60d8d0d1b00081a99893a6e67cb44d0c92
tests/unit/input_bindings_test.cpp ecef7f3fab80a693679124f5d353de61165c6888b8e7aa89a6159b71132ca35d bee6f31e71cd9c8e8a4ea263d1e880b01023053e75c0be871c13001b7a0871b7
tests/unit/install_test.cpp 59a2e38a2fae6bc6cbcbb1820a4c1a235b1587fc1e7c31171b2426d241eac278 6ba23eaa490aea9fa98ee2c7f008b5d204020abe1081f230965ce99630f66822
tests/unit/render_scale_test.cpp c7429fd4f00b7037524bd32c6090ac65558bad57bcca17aa9027de14652545f2 cb7c8a46104083e17bf7b4163abfa5a2a367b5a533e18b47b675571a35e6d484
tests/unit/save_files_test.cpp 8deafd296768cd0521afd9e6f06124f794094d09a7fa0c0daea6dff813d8afdb ec1ec190d05431593977e0a8b42c024b8dd551a444fd3946f578c949ea252898
tests/unit/save_store_test.cpp f323110b7b41a63a0b86951d3a7340080a14bb2404a1822762e7c75fe8028917 e72b062bc75baf8ea30e49fb77222ff9faf54dd2f62ceadb4ac4ca0136919557
tests/unit/settings_test.cpp bc9f465c3a317f78495fff9679131940782c01b5d18f3822d097ce9b4f8e7f36 5c3470cb8c2031605ca46b57c22408ee5619f8cc2a24438a9ec44a25dcc50cd0
tests/unit/text_entry_test.cpp 256272cd068a76e69feb2f2bfe0b2959d2b031398b316f0150270223e7783879 578d33a7961eb49559cb321b535d1fa8c3557133878d3f7224f3d62f45c09bc3
.clang-format e395beaa072bb3a70b75d4cfbceb11ab920c7bfbee25bac1e65ed926c72b3c18 e395beaa072bb3a70b75d4cfbceb11ab920c7bfbee25bac1e65ed926c72b3c18
.gitignore e897d73a18eed22a2bdf122cdb106da753b2e5d0759f75eb140b009a8641cb44 e897d73a18eed22a2bdf122cdb106da753b2e5d0759f75eb140b009a8641cb44
CMakeLists.txt 172b5782eae0bdda4d447858e536062fed29de31a4549dff7d22b4b35824e38c 45e05356cf656147476b5d67fcb64fbfbac0b4df14af48f9fa377e8a16e531d0
pyproject.toml 4ac3703cc28f121ff2896cc5d5fb4c27af47349f19ea58b3dd30e74d815e33f9 4ac3703cc28f121ff2896cc5d5fb4c27af47349f19ea58b3dd30e74d815e33f9
-->
