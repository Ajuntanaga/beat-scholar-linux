#pragma once

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>

namespace bsl::detail {
// Mathematical lookup tables: log(1+i/256), 512/(256+i), and 2^(i/64).
// Constant initialization keeps table construction off the audio thread.
inline constexpr std::array<double, 257> compressorLog {
    0x0.0p+0, 0x1.ff00aa2b10bc0p-9, 0x1.fe02a6b106789p-8, 0x1.7dc475f810a77p-7,
    0x1.fc0a8b0fc03e4p-7, 0x1.3cea44346a575p-6, 0x1.7b91b07d5b11bp-6, 0x1.b9fc027af9198p-6,
    0x1.f829b0e783300p-6, 0x1.1b0d98923d980p-5, 0x1.39e87b9febd60p-5, 0x1.58a5bafc8e4d5p-5,
    0x1.77458f632dcfcp-5, 0x1.95c830ec8e3ebp-5, 0x1.b42dd711971bfp-5, 0x1.d276b8adb0b52p-5,
    0x1.f0a30c01162a6p-5, 0x1.075983598e471p-4, 0x1.16536eea37ae1p-4, 0x1.253f62f0a1417p-4,
    0x1.341d7961bd1d1p-4, 0x1.42edcbea646f0p-4, 0x1.51b073f06183fp-4, 0x1.60658a93750c4p-4,
    0x1.6f0d28ae56b4cp-4, 0x1.7da766d7b12cdp-4, 0x1.8c345d6319b21p-4, 0x1.9ab42462033adp-4,
    0x1.a926d3a4ad563p-4, 0x1.b78c82bb0eda1p-4, 0x1.c5e548f5bc743p-4, 0x1.d4313d66cb35dp-4,
    0x1.e27076e2af2e6p-4, 0x1.f0a30c01162a6p-4, 0x1.fec9131dbeabbp-4, 0x1.0671512ca596ep-3,
    0x1.0d77e7cd08e59p-3, 0x1.14785846742acp-3, 0x1.1b72ad52f67a0p-3, 0x1.2266f190a5acbp-3,
    0x1.29552f81ff523p-3, 0x1.303d718e47fd3p-3, 0x1.371fc201e8f74p-3, 0x1.3dfc2b0ecc62ap-3,
    0x1.44d2b6ccb7d1ep-3, 0x1.4ba36f39a55e5p-3, 0x1.526e5e3a1b438p-3, 0x1.59338d9982086p-3,
    0x1.5ff3070a793d4p-3, 0x1.66acd4272ad51p-3, 0x1.6d60fe719d21dp-3, 0x1.740f8f54037a5p-3,
    0x1.7ab890210d909p-3, 0x1.815c0a14357ebp-3, 0x1.87fa06520c911p-3, 0x1.8e928de886d41p-3,
    0x1.9525a9cf456b4p-3, 0x1.9bb362e7dfb83p-3, 0x1.a23bc1fe2b563p-3, 0x1.a8becfc882f19p-3,
    0x1.af3c94e80bff3p-3, 0x1.b5b519e8fb5a4p-3, 0x1.bc286742d8cd6p-3, 0x1.c2968558c18c1p-3,
    0x1.c8ff7c79a9a22p-3, 0x1.cf6354e09c5dcp-3, 0x1.d5c216b4fbb91p-3, 0x1.dc1bca0abec7dp-3,
    0x1.e27076e2af2e6p-3, 0x1.e8c0252aa5a60p-3, 0x1.ef0adcbdc5936p-3, 0x1.f550a564b7b37p-3,
    0x1.fb9186d5e3e2bp-3, 0x1.00e6c45ad501dp-2, 0x1.0402594b4d041p-2, 0x1.071b85fcd590dp-2,
    0x1.0a324e27390e3p-2, 0x1.0d46b579ab74bp-2, 0x1.1058bf9ae4ad5p-2, 0x1.136870293a8b0p-2,
    0x1.1675cababa60ep-2, 0x1.1980d2dd4236fp-2, 0x1.1c898c16999fbp-2, 0x1.1f8ff9e48a2f3p-2,
    0x1.22941fbcf7966p-2, 0x1.2596010df763ap-2, 0x1.2895a13de86a3p-2, 0x1.2b9303ab89d25p-2,
    0x1.2e8e2bae11d31p-2, 0x1.31871c9544185p-2, 0x1.347dd9a987d55p-2, 0x1.3772662bfd85bp-2,
    0x1.3a64c556945eap-2, 0x1.3d54fa5c1f710p-2, 0x1.404308686a7e4p-2, 0x1.432ef2a04e814p-2,
    0x1.4618bc21c5ec2p-2, 0x1.49006804009d1p-2, 0x1.4be5f957778a1p-2, 0x1.4ec973260026ap-2,
    0x1.51aad872df82dp-2, 0x1.548a2c3add263p-2, 0x1.5767717455a6cp-2, 0x1.5a42ab0f4cfe2p-2,
    0x1.5d1bdbf5809cap-2, 0x1.5ff3070a793d4p-2, 0x1.62c82f2b9c795p-2, 0x1.659b57303e1f3p-2,
    0x1.686c81e9b14afp-2, 0x1.6b3bb2235943ep-2, 0x1.6e08eaa2ba1e4p-2, 0x1.70d42e2789236p-2,
    0x1.739d7f6bbd007p-2, 0x1.7664e1239dbcfp-2, 0x1.792a55fdd47a2p-2, 0x1.7bede0a37afc0p-2,
    0x1.7eaf83b82afc3p-2, 0x1.816f41da0d496p-2, 0x1.842d1da1e8b17p-2, 0x1.86e919a330ba0p-2,
    0x1.89a3386c1425bp-2, 0x1.8c5b7c858b48bp-2, 0x1.8f11e873662c7p-2, 0x1.91c67eb45a83ep-2,
    0x1.947941c2116fbp-2, 0x1.972a341135158p-2, 0x1.99d958117e08bp-2, 0x1.9c86b02dc0863p-2,
    0x1.9f323ecbf984cp-2, 0x1.a1dc064d5b995p-2, 0x1.a484090e5bb0ap-2, 0x1.a72a4966bd9eap-2,
    0x1.a9cec9a9a084ap-2, 0x1.ac718c258b0e4p-2, 0x1.af1293247786bp-2, 0x1.b1b1e0ebdfc5bp-2,
    0x1.b44f77bcc8f63p-2, 0x1.b6eb59d3cf35ep-2, 0x1.b9858969310fbp-2, 0x1.bc1e08b0dad0ap-2,
    0x1.beb4d9da71b7cp-2, 0x1.c149ff115f027p-2, 0x1.c3dd7a7cdad4dp-2, 0x1.c66f4e3ff6ff8p-2,
    0x1.c8ff7c79a9a22p-2, 0x1.cb8e0744d7acap-2, 0x1.ce1af0b85f3ebp-2, 0x1.d0a63ae721e64p-2,
    0x1.d32fe7e00ebd5p-2, 0x1.d5b7f9ae2c684p-2, 0x1.d83e7258a2f3ep-2, 0x1.dac353e2c5954p-2,
    0x1.dd46a04c1c4a1p-2, 0x1.dfc859906d5b5p-2, 0x1.e24881a7c6c26p-2, 0x1.e4c71a8687704p-2,
    0x1.e744261d68788p-2, 0x1.e9bfa659861f5p-2, 0x1.ec399d2468cc0p-2, 0x1.eeb20c640ddf4p-2,
    0x1.f128f5faf06edp-2, 0x1.f39e5bc811e5cp-2, 0x1.f6123fa7028acp-2, 0x1.f884a36fe9ec2p-2,
    0x1.faf588f78f31fp-2, 0x1.fd64f20f61572p-2, 0x1.ffd2e0857f498p-2, 0x1.011fab125ff8ap-1,
    0x1.02552a5a5d0ffp-1, 0x1.0389eefce633bp-1, 0x1.04bdf9da926d2p-1, 0x1.05f14bd26459cp-1,
    0x1.0723e5c1cdf40p-1, 0x1.0855c884b450ep-1, 0x1.0986f4f573521p-1, 0x1.0ab76bece14d2p-1,
    0x1.0be72e4252a83p-1, 0x1.0d163ccb9d6b8p-1, 0x1.0e44985d1cc8cp-1, 0x1.0f7241c9b497dp-1,
    0x1.109f39e2d4c97p-1, 0x1.11cb81787ccf8p-1, 0x1.12f719593efbcp-1, 0x1.1422025243d45p-1,
    0x1.154c3d2f4d5eap-1, 0x1.1675cababa60ep-1, 0x1.179eabbd899a1p-1, 0x1.18c6e0ff5cf06p-1,
    0x1.19ee6b467c96fp-1, 0x1.1b154b57da29fp-1, 0x1.1c3b81f713c25p-1, 0x1.1d610fe677003p-1,
    0x1.1e85f5e7040d0p-1, 0x1.1faa34b87094cp-1, 0x1.20cdcd192ab6ep-1, 0x1.21f0bfc65beecp-1,
    0x1.23130d7bebf43p-1, 0x1.2434b6f483934p-1, 0x1.2555bce98f7cbp-1, 0x1.26762013430e0p-1,
    0x1.2795e1289b11bp-1, 0x1.28b500df60783p-1, 0x1.29d37fec2b08bp-1, 0x1.2af15f02640adp-1,
    0x1.2c0e9ed448e8cp-1, 0x1.2d2b4012edc9ep-1, 0x1.2e47436e40268p-1, 0x1.2f62a99509546p-1,
    0x1.307d7334f10bep-1, 0x1.3197a0fa7fe6ap-1, 0x1.32b1339121d71p-1, 0x1.33ca2ba328995p-1,
    0x1.34e289d9ce1d3p-1, 0x1.35fa4edd36ea0p-1, 0x1.37117b54747b6p-1, 0x1.38280fe58797fp-1,
    0x1.393e0d3562a1ap-1, 0x1.3a5373e7ebdfap-1, 0x1.3b68449fffc23p-1, 0x1.3c7c7fff73206p-1,
    0x1.3d9026a7156fbp-1, 0x1.3ea33936b2f5cp-1, 0x1.3fb5b84d16f42p-1, 0x1.40c7a4880dce9p-1,
    0x1.41d8fe84672aep-1, 0x1.42e9c6ddf80bfp-1, 0x1.43f9fe2f9ce67p-1, 0x1.4509a5133bb0ap-1,
    0x1.4618bc21c5ec2p-1, 0x1.472743f33aaadp-1, 0x1.48353d1ea88dfp-1, 0x1.4942a83a2fc07p-1,
    0x1.4a4f85db03ebbp-1, 0x1.4b5bd6956e274p-1, 0x1.4c679afccee3ap-1, 0x1.4d72d3a39fd00p-1,
    0x1.4e7d811b75bb1p-1, 0x1.4f87a3f5026e9p-1, 0x1.50913cc01686bp-1, 0x1.519a4c0ba3446p-1,
    0x1.52a2d265bc5abp-1, 0x1.53aad05b99b7dp-1, 0x1.54b2467999498p-1, 0x1.55b9354b40bcdp-1,
    0x1.56bf9d5b3f399p-1, 0x1.57c57f336f191p-1, 0x1.58cadb5cd7989p-1, 0x1.59cfb25fae87ep-1,
    0x1.5ad404c359f2dp-1, 0x1.5bd7d30e71c73p-1, 0x1.5cdb1dc6c1765p-1, 0x1.5ddde57149923p-1,
    0x1.5ee02a9241675p-1, 0x1.5fe1edad18919p-1, 0x1.60e32f44788d9p-1, 0x1.61e3efda46467p-1,
    0x1.62e42fefa39efp-1,
};

inline constexpr std::array<double, 257> compressorReciprocal {
    0x1.0000000000000p+1, 0x1.fe01fe01fe020p+0, 0x1.fc07f01fc07f0p+0, 0x1.fa11caa01fa12p+0,
    0x1.f81f81f81f820p+0, 0x1.f6310aca0dbb5p+0, 0x1.f44659e4a4271p+0, 0x1.f25f644230ab5p+0,
    0x1.f07c1f07c1f08p+0, 0x1.ee9c7f8458e02p+0, 0x1.ecc07b301ecc0p+0, 0x1.eae807aba01ebp+0,
    0x1.e9131abf0b767p+0, 0x1.e741aa59750e4p+0, 0x1.e573ac901e574p+0, 0x1.e3a9179dc1a73p+0,
    0x1.e1e1e1e1e1e1ep+0, 0x1.e01e01e01e01ep+0, 0x1.de5d6e3f8868ap+0, 0x1.dca01dca01dcap+0,
    0x1.dae6076b981dbp+0, 0x1.d92f2231e7f8ap+0, 0x1.d77b654b82c34p+0, 0x1.d5cac807572b2p+0,
    0x1.d41d41d41d41dp+0, 0x1.d272ca3fc5b1ap+0, 0x1.d0cb58f6ec074p+0, 0x1.cf26e5c44bfc6p+0,
    0x1.cd85689039b0bp+0, 0x1.cbe6d9601cbe7p+0, 0x1.ca4b3055ee191p+0, 0x1.c8b265afb8a42p+0,
    0x1.c71c71c71c71cp+0, 0x1.c5894d10d4986p+0, 0x1.c3f8f01c3f8f0p+0, 0x1.c26b5392ea01cp+0,
    0x1.c0e070381c0e0p+0, 0x1.bf583ee868d8bp+0, 0x1.bdd2b899406f7p+0, 0x1.bc4fd65883e7bp+0,
    0x1.bacf914c1bad0p+0, 0x1.b951e2b18ff23p+0, 0x1.b7d6c3dda338bp+0, 0x1.b65e2e3beee05p+0,
    0x1.b4e81b4e81b4fp+0, 0x1.b37484ad806cep+0, 0x1.b2036406c80d9p+0, 0x1.b094b31d922a4p+0,
    0x1.af286bca1af28p+0, 0x1.adbe87f94905ep+0, 0x1.ac5701ac5701bp+0, 0x1.aaf1d2f87ebfdp+0,
    0x1.a98ef606a63bep+0, 0x1.a82e65130e159p+0, 0x1.a6d01a6d01a6dp+0, 0x1.a574107688a4ap+0,
    0x1.a41a41a41a41ap+0, 0x1.a2c2a87c51ca0p+0, 0x1.a16d3f97a4b02p+0, 0x1.a01a01a01a01ap+0,
    0x1.9ec8e951033d9p+0, 0x1.9d79f176b682dp+0, 0x1.9c2d14ee4a102p+0, 0x1.9ae24ea5510dap+0,
    0x1.999999999999ap+0, 0x1.9852f0d8ec0ffp+0, 0x1.970e4f80cb872p+0, 0x1.95cbb0be377aep+0,
    0x1.948b0fcd6e9e0p+0, 0x1.934c67f9b2ce6p+0, 0x1.920fb49d0e229p+0, 0x1.90d4f120190d5p+0,
    0x1.8f9c18f9c18fap+0, 0x1.8e6527af1373fp+0, 0x1.8d3018d3018d3p+0, 0x1.8bfce8062ff3ap+0,
    0x1.8acb90f6bf3aap+0, 0x1.899c0f601899cp+0, 0x1.886e5f0abb04ap+0, 0x1.87427bcc092b9p+0,
    0x1.8618618618618p+0, 0x1.84f00c2780614p+0, 0x1.83c977ab2beddp+0, 0x1.82a4a0182a4a0p+0,
    0x1.8181818181818p+0, 0x1.8060180601806p+0, 0x1.7f405fd017f40p+0, 0x1.7e225515a4f1dp+0,
    0x1.7d05f417d05f4p+0, 0x1.7beb3922e017cp+0, 0x1.7ad2208e0ecc3p+0, 0x1.79baa6bb6398bp+0,
    0x1.78a4c8178a4c8p+0, 0x1.77908119ac60dp+0, 0x1.767dce434a9b1p+0, 0x1.756cac201756dp+0,
    0x1.745d1745d1746p+0, 0x1.734f0c541fe8dp+0, 0x1.724287f46debcp+0, 0x1.713786d9c7c09p+0,
    0x1.702e05c0b8170p+0, 0x1.6f26016f26017p+0, 0x1.6e1f76b4337c7p+0, 0x1.6d1a62681c861p+0,
    0x1.6c16c16c16c17p+0, 0x1.6b1490aa31a3dp+0, 0x1.6a13cd1537290p+0, 0x1.691473a88d0c0p+0,
    0x1.6816816816817p+0, 0x1.6719f3601671ap+0, 0x1.661ec6a5122f9p+0, 0x1.6524f853b4aa3p+0,
    0x1.642c8590b2164p+0, 0x1.63356b88ac0dep+0, 0x1.623fa77016240p+0, 0x1.614b36831ae94p+0,
    0x1.6058160581606p+0, 0x1.5f66434292dfcp+0, 0x1.5e75bb8d015e7p+0, 0x1.5d867c3ece2a5p+0,
    0x1.5c9882b931057p+0, 0x1.5babcc647fa91p+0, 0x1.5ac056b015ac0p+0, 0x1.59d61f123ccaap+0,
    0x1.58ed2308158edp+0, 0x1.5805601580560p+0, 0x1.571ed3c506b3ap+0, 0x1.56397ba7c52e2p+0,
    0x1.5555555555555p+0, 0x1.54725e6bb82fep+0, 0x1.5390948f40febp+0, 0x1.52aff56a8054bp+0,
    0x1.51d07eae2f815p+0, 0x1.50f22e111c4c5p+0, 0x1.5015015015015p+0, 0x1.4f38f62dd4c9bp+0,
    0x1.4e5e0a72f0539p+0, 0x1.4d843bedc2c4cp+0, 0x1.4cab88725af6ep+0, 0x1.4bd3edda68fe1p+0,
    0x1.4afd6a052bf5bp+0, 0x1.4a27fad76014ap+0, 0x1.49539e3b2d067p+0, 0x1.4880522014880p+0,
    0x1.47ae147ae147bp+0, 0x1.46dce34596066p+0, 0x1.460cbc7f5cf9ap+0, 0x1.453d9e2c776cap+0,
    0x1.446f86562d9fbp+0, 0x1.43a2730abee4dp+0, 0x1.42d6625d51f87p+0, 0x1.420b5265e5951p+0,
    0x1.4141414141414p+0, 0x1.40782d10e6566p+0, 0x1.3fb013fb013fbp+0, 0x1.3ee8f42a5af07p+0,
    0x1.3e22cbce4a902p+0, 0x1.3d5d991aa75c6p+0, 0x1.3c995a47babe7p+0, 0x1.3bd60d9232955p+0,
    0x1.3b13b13b13b14p+0, 0x1.3a524387ac822p+0, 0x1.3991c2c187f63p+0, 0x1.38d22d366088ep+0,
    0x1.3813813813814p+0, 0x1.3755bd1c945eep+0, 0x1.3698df3de0748p+0, 0x1.35dce5f9f2af8p+0,
    0x1.3521cfb2b78c1p+0, 0x1.34679ace01346p+0, 0x1.33ae45b57bcb2p+0, 0x1.32f5ced6a1dfap+0,
    0x1.323e34a2b10bfp+0, 0x1.3187758e9ebb6p+0, 0x1.30d190130d190p+0, 0x1.301c82ac40260p+0,
    0x1.2f684bda12f68p+0, 0x1.2eb4ea1fed14bp+0, 0x1.2e025c04b8097p+0, 0x1.2d50a012d50a0p+0,
    0x1.2c9fb4d812ca0p+0, 0x1.2bef98e5a3711p+0, 0x1.2b404ad012b40p+0, 0x1.2a91c92f3c105p+0,
    0x1.29e4129e4129ep+0, 0x1.293725bb804a5p+0, 0x1.288b01288b013p+0, 0x1.27dfa38a1ce4dp+0,
    0x1.27350b8812735p+0, 0x1.268b37cd60127p+0, 0x1.25e22708092f1p+0, 0x1.2539d7e9177b2p+0,
    0x1.2492492492492p+0, 0x1.23eb79717605bp+0, 0x1.23456789abcdfp+0, 0x1.22a0122a0122ap+0,
    0x1.21fb78121fb78p+0, 0x1.21579804855e6p+0, 0x1.20b470c67c0d9p+0, 0x1.2012012012012p+0,
    0x1.1f7047dc11f70p+0, 0x1.1ecf43c7fb84cp+0, 0x1.1e2ef3b3fb874p+0, 0x1.1d8f5672e4abdp+0,
    0x1.1cf06ada2811dp+0, 0x1.1c522fc1ce059p+0, 0x1.1bb4a4046ed29p+0, 0x1.1b17c67f2bae3p+0,
    0x1.1a7b9611a7b96p+0, 0x1.19e0119e0119ep+0, 0x1.19453808ca29cp+0, 0x1.18ab083902bdbp+0,
    0x1.1811811811812p+0, 0x1.1778a191bd684p+0, 0x1.16e0689427379p+0, 0x1.1648d50fc3201p+0,
    0x1.15b1e5f75270dp+0, 0x1.151b9a3fdd5c9p+0, 0x1.1485f0e0acd3bp+0, 0x1.13f0e8d344724p+0,
    0x1.135c81135c811p+0, 0x1.12c8b89edc0acp+0, 0x1.12358e75d3033p+0, 0x1.11a3019a74826p+0,
    0x1.1111111111111p+0, 0x1.107fbbe011080p+0, 0x1.0fef010fef011p+0, 0x1.0f5edfab325a2p+0,
    0x1.0ecf56be69c90p+0, 0x1.0e40655826011p+0, 0x1.0db20a88f4696p+0, 0x1.0d24456359e3ap+0,
    0x1.0c9714fbcda3bp+0, 0x1.0c0a7868b4171p+0, 0x1.0b7e6ec259dc8p+0, 0x1.0af2f722eecb5p+0,
    0x1.0a6810a6810a7p+0, 0x1.09ddba6af8360p+0, 0x1.0953f39010954p+0, 0x1.08cabb37565e2p+0,
    0x1.0842108421084p+0, 0x1.07b9f29b8eae2p+0, 0x1.073260a47f7c6p+0, 0x1.06ab59c7912fbp+0,
    0x1.0624dd2f1a9fcp+0, 0x1.059eea0727586p+0, 0x1.05197f7d73404p+0, 0x1.04949cc1664c5p+0,
    0x1.0410410410410p+0, 0x1.038c6b78247fcp+0, 0x1.03091b51f5e1ap+0, 0x1.02864fc7729e9p+0,
    0x1.0204081020408p+0, 0x1.0182436517a37p+0, 0x1.0101010101010p+0, 0x1.0080402010080p+0,
    0x1.0000000000000p+0,
};

inline constexpr std::array<double, 64> compressorExp {
    0x1.0000000000000p+0, 0x1.02c9a3e778061p+0, 0x1.059b0d3158574p+0, 0x1.0874518759bc8p+0,
    0x1.0b5586cf9890fp+0, 0x1.0e3ec32d3d1a2p+0, 0x1.11301d0125b51p+0, 0x1.1429aaea92de0p+0,
    0x1.172b83c7d517bp+0, 0x1.1a35beb6fcb75p+0, 0x1.1d4873168b9aap+0, 0x1.2063b88628cd6p+0,
    0x1.2387a6e756238p+0, 0x1.26b4565e27cddp+0, 0x1.29e9df51fdee1p+0, 0x1.2d285a6e4030bp+0,
    0x1.306fe0a31b715p+0, 0x1.33c08b26416ffp+0, 0x1.371a7373aa9cbp+0, 0x1.3a7db34e59ff7p+0,
    0x1.3dea64c123422p+0, 0x1.4160a21f72e2ap+0, 0x1.44e086061892dp+0, 0x1.486a2b5c13cd0p+0,
    0x1.4bfdad5362a27p+0, 0x1.4f9b2769d2ca7p+0, 0x1.5342b569d4f82p+0, 0x1.56f4736b527dap+0,
    0x1.5ab07dd485429p+0, 0x1.5e76f15ad2148p+0, 0x1.6247eb03a5585p+0, 0x1.6623882552225p+0,
    0x1.6a09e667f3bcdp+0, 0x1.6dfb23c651a2fp+0, 0x1.71f75e8ec5f74p+0, 0x1.75feb564267c9p+0,
    0x1.7a11473eb0187p+0, 0x1.7e2f336cf4e62p+0, 0x1.82589994cce13p+0, 0x1.868d99b4492edp+0,
    0x1.8ace5422aa0dbp+0, 0x1.8f1ae99157736p+0, 0x1.93737b0cdc5e5p+0, 0x1.97d829fde4e50p+0,
    0x1.9c49182a3f090p+0, 0x1.a0c667b5de565p+0, 0x1.a5503b23e255dp+0, 0x1.a9e6b5579fdbfp+0,
    0x1.ae89f995ad3adp+0, 0x1.b33a2b84f15fbp+0, 0x1.b7f76f2fb5e47p+0, 0x1.bcc1e904bc1d2p+0,
    0x1.c199bdd85529cp+0, 0x1.c67f12e57d14bp+0, 0x1.cb720dcef9069p+0, 0x1.d072d4a07897cp+0,
    0x1.d5818dcfba487p+0, 0x1.da9e603db3285p+0, 0x1.dfc97337b9b5fp+0, 0x1.e502ee78b3ff6p+0,
    0x1.ea4afa2a490dap+0, 0x1.efa1bee615a27p+0, 0x1.f50765b6e4540p+0, 0x1.fa7c1819e90d8p+0,
};

// Match the reference's double-intermediate polynomial, including its final
// float rounding. A correctly rounded libm pow differs at rare inputs and those
// differences become audible after the nonlinear distortion feedback stage.
#if defined(__GNUC__) && !defined(__clang__)
__attribute__((optimize("fp-contract=off")))
#endif
inline float compressorPower(float base, float exponent) noexcept
{
#if defined(__clang__)
#pragma clang fp contract(off)
#endif
    // The compressor calls this only above threshold, with negative exponents.
    if (!std::isfinite(base) || base < 1.0f || !std::isfinite(exponent)
        || exponent < -1.0f || exponent > 0.0f)
        return static_cast<float>(std::pow(static_cast<double>(base), exponent));
    if (base == 1.0f || exponent == 0.0f)
        return 1.0f;

    constexpr double ln2 = 0x1.62e42fefa39efp-1;
    double logarithm;
    const double x = base;
    if (base < 1.0625f)
    {
        const double d = x - 1.0;
        const double q = d / (d + 2.0);
        const double u = q + q;
        const double u2 = u * u;
        const double u3 = u * u2;
        const double u7 = (u2 * u2) * u3;
        const double low = u3 * (u2 * 0x1.9999999bac6d4p-7 + 0x1.55555555554e6p-4);
        const double high = u7 * (u2 * 0x1.c8034c85dfff0p-12 + 0x1.2492307f1519fp-9);
        logarithm = d + ((low + high) - q * d);
    }
    else
    {
        const auto bits = std::bit_cast<std::uint64_t>(x);
        const auto mantissa = bits & ((std::uint64_t{1} << 52) - 1);
        const auto index = static_cast<std::size_t>((mantissa >> 44) + ((mantissa >> 43) & 1));
        const double fraction = std::bit_cast<double>(mantissa | (std::uint64_t{1022} << 52));
        const double rounded = static_cast<double>(256 + index) / 512.0;
        const double r = (rounded - fraction) * compressorReciprocal[index];
        const double correction = r + (r * r) * (r * (1.0 / 3.0) + 0.5);
        logarithm = static_cast<double>(static_cast<int>(bits >> 52) - 1023) * ln2
            + compressorLog[index] - correction;
    }
    const double z = static_cast<double>(exponent) * logarithm;
    const int n = static_cast<int>(std::nearbyint(z * 0x1.71547652b82fep+6));
    const double r = z - static_cast<double>(n) * 0x1.62e42fefa39efp-7;
    const double correction = r + (r * r) * (r * (1.0 / 6.0) + 0.5);
    const double table = compressorExp[static_cast<unsigned>(n) & 63u];
    return static_cast<float>(std::ldexp(table * correction + table, n >> 6));
}
} // namespace bsl::detail
