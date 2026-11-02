# Carta do projeto

O projeto pesquisa uma plataforma modular Darwin/XNU em ARM64, iniciando no Penang e usando Sofiar como futura qualificação de generalidade. Suporte multiplataforma é objetivo, não capacidade existente. O produto inicial segue [ADR-0004](contracts/platform/foundation/docs/adr/ADR-0004-no-pac-userland-and-kernel-base.md) e [ADR-0006](contracts/platform/foundation/docs/adr/ADR-0006-delivery-scope-and-tracks.md).

## Escopo aceito

Penang: SM4350-AC/holi, Adreno 619; iPadOS 18.7.8/22H352/iPad7,11; XNU público 11417.140.69. Preservar inputs Apple, bytes e procedência; registrar adaptações do runtime.
D1 requer 11417 ARMv8.2 via fastboot boot sem gravação; D2 launchd original até SpringBoard; D3 dispositivo Metal Adreno e composição medida por contadores; D4 toque e retorno Android. Nenhum está demonstrado.
Sofiar: P1 headless, modelo/codename/SoC declarados, identidade e canais físicos por qualificar. Seu ingresso foi solicitado pelo responsável; não cria aceite tácito de ADR ou driver.

## Princípios

Upstream-first, interfaces versionadas, backends por plataforma, capacidades medidas, configuração declarativa, provedores completos no escopo anunciado, fail-closed e proveniência.
Um hash identifica bytes; não autentica fabricante nem prova execução. Compilar não é executar. Um resultado host ou QEMU não prova dispositivo.

Não são entregas atuais: ROM instalável, suporte universal Qualcomm, Apple ID/App Store/DRM, telefonia/VoLTE, equivalência Secure Boot/SEP/PPL/SPTM/TXM ou distribuição de firmware/OS.
Não há autorização de hardware, CI ou publicação decorrente desta árvore. O próximo trabalho precisa de escopo próprio, sem repetir o histórico indiscriminadamente.

Autoria: síntese documental W1 por Codex, sob ordem do responsável, a partir das fontes selecionadas em contracts/platform/foundation/evidence/source-register.json. As decisões originais preservam seus autores e datas.
