# Aplicativos e userland — plano canônico

Síntese atualizada a partir de APP-COMPATIBILITY-PLAN da pesquisa M3, fonte S15, com autoria/procedência preservadas no registro. Nenhum aplicativo ou launchd/SpringBoard foi demonstrado no porte.

A intenção é execução arm64 no ambiente iPadOS 18.7.8 do iPad7,11, preservando arquivos Apple e registrando adaptações. Compartilhar ISA não entrega ABI, dyld, Mach/MIG, VM, TLS, ObjC/Swift, XPC ou frameworks.
XNU 8796 não atende automaticamente o userland 18; a base pública alvo é 11417.140.69. Linux .ko/ELF/Bionic não se tornam IOKit por cópia.

## Provas por camada

| Camada | Prova exigida |
|---|---|
| ISA/ABI | Programa Mach-O próprio, chamadas/retornos/TLS e recursos reais de CPU |
| Kernel e serviços | IPC/MIG/VM, dyld/cache, XPC, init/launchd observados |
| Interface | App próprio e serviços UIKit/IOSurface/CoreAnimation |
| GPU | Shaders e recursos corretos, contadores Adreno e composição, não só IR3 host |
| Política | Assinatura/entitlements e rejeições reais, sandbox/TCC/isolamento |
| Distribuição | Direitos e serviços/DRM tratados separadamente; sem identidade Apple falsa |

M9 possui frontend AIR→NIR→IR3 para subset limitado e artefatos host/cross-build. Recibos independentes O2/O3 continuam abertos no escopo auditado; nenhum resultado prova execução GPU.
O driver AGX do A10 não atende a A619. Precisamos de IOGPU/IOSurface, memória, sincronização, comandos e backend de GPU.

A sequência proposta é programa próprio mínimo -> serviço IPC/XPC -> app UIKit -> desenho/composição GPU -> APIs de input/áudio/rede -> casos autorizados de aplicativos. Segurança negativa precisa acompanhar cada contrato, sem aceitar-tudo.
App Store/Apple ID/FairPlay não são resultado presumido; modem de dados não implica telefonia/VoLTE. Estes são marcos distintos de D1-D4.

Fontes: S01/S11/S15/S19/S20 no registro; critérios aceitos em [ADR-0006](../contracts/platform/foundation/docs/adr/ADR-0006-delivery-scope-and-tracks.md).
