# ADR-0004 — Userland sem PAC e base de kernel para a família Snapdragon ARMv8.2
Data: 2026-10-27 · Status: **ACEITO** pelo responsável em 2026-10-27 (alvo iPadOS 18.7.8 iPad7,11) · Complementa ADR-0002 (não o revoga).

> Condição F07 **satisfeita** em 2026-10-27: 57 cabeçalhos de dyld shared cache no cryptex SystemOS do iPad7,11 22H352, todos `arm64`, nenhum `arm64e`; slide info v2 (`manifests/ipados-18.7.8-ipad7-dyld-cache.json`). Chave AEA publicada conferida com o serviço WKMS da Apple.

## Contexto (somente evidência verificada; ver `manifests/xnu-abi-assessment.json`)
- **F05 (MEASURED):** o penang não expõe PAC (sem `paca/pacg` nos 8 núcleos; MIDR 6× `0x805` Kryo 4xx Silver + 2× `0x804` Kryo 4xx Gold).
- **F01 (E3):** o iOS 18.7.8 22H352 de iPhone roda `xnu-11417.140.69.706.9`, **ARM64E**. Todo iPhone suportado pelo iOS 18 é A12+.
- **F02 (E3):** o **mesmo build 22H352** foi publicado como iPadOS 18.7.8 para o iPad (7ª geração, iPad7,11/12, A10/T8010), com kernel **ARM64_ALL** (sem ABI arm64e).
- **F06 (INFERRED):** instruções PAC fora do espaço de *hint* são UNDEFINED em ARMv8.2 → o userland arm64e do iPhone só roda no penang **TRANSFORMED** (estilo Bergamota), nunca ORIGINAL.
- **F08 (E2):** a config de máquina genérica `BCM2837`, usada como base por HackiOS e ios-sweet, existe no 8796 e **foi removida** no 11215/11417; o `MakeInc.def` do 11417 só lista SoCs Apple e VMAPPLE.
- **F10 (E2):** o contrato MIG 3404/3405 (`mach_ports_*` → `_kernelrpc_mach_ports_*3`) mudou no 11215; um kernel 8796 não atende um userland 18.

## Decisão
1. **Userland 18.x preservável (ORIGINAL):** adotar como alvo primário o **iPadOS 18.7.8 22H352 do iPad7,11**, condicionado a F07 (provar no IPSW que o shared cache e os binários de plataforma são arm64 puro). Se F07 falhar, o alvo "18.x ORIGINAL em CPU sem PAC" é declarado **inviável** e só resta TRANSFORMED, rotulado como tal.
2. **iOS 18.7.8 de iPhone permanece alvo de pesquisa** apenas em modo TRANSFORMED, separado e nunca anunciado como original.
3. **Kernel:** dois trilhos versionados e independentes, sem patch global:
   - Trilho A (bring-up, M2): `xnu-8796.121.2` + base BCM2837 da comunidade, para validar loader/ADT/BSP do penang com userland iOS 16.x (iPhone X, arm64), igual ao estado da arte público.
   - Trilho B (alvo 18.x): `xnu-11417.140.69` + **nova config de máquina genérica ARMv8.2** (`-mcpu` fixado para Cortex-A55/A76, `CONFIG_EXCLAVES=0`, `CONFIG_SPTM=0`, `__ARM_16K_PG__`), portando o delta BCM2837 do 8796. Isso é trabalho nosso, não existe upstream.
4. Separação SoC/placa: interfaces versionadas `soc/qcom-<familia>/` e `board/<codinome>/`, com o delta por versão de XNU em `xnu/<familia>/`, seguindo o modelo `devices/<nome>/` do HackiOS (E1).

## Consequências
- O produto "iOS 18 original no Moto G53" passa a significar **iPadOS 18.7.8 de iPad7,11** (identidade de iPad, UI de iPad). É uma mudança de produto que só o responsável pode aceitar.
- Nenhuma equivalência com Secure Boot, SEP, SPTM, TXM ou Exclaves é reivindicada (F12).
- Kernel e drivers da comunidade escritos para 8796 precisam de *port* de código para 11417 (F14, F15, F16), não de relink.

## Alternativas rejeitadas
- Rodar o userland 18 de iPhone sobre kernel 8796: quebra F10 e F13 (slide v5 inexistente no 8796).
- Emular PAC em software/armadilha: custo e risco altos, e o resultado seria RUNTIME_ADAPTED sem garantias; mantido apenas como pesquisa.

## Critério de reabertura
F07 refutado; ou medição (F19) mostrar que os núcleos do penang não suportam grânulo de 16 KiB.
