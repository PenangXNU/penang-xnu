# Penang XNU — resumo em português

Pesquisa para executar o kernel XNU público da Apple e, futuramente, o userland do iPadOS **sem modificações**, fornecido pelo próprio usuário, em hardware ARM64 que não é da Apple. O primeiro aparelho é o Motorola Moto G53 5G (`penang`, Qualcomm SM4350/holi, Adreno 619); o QEMU `virt` é o ambiente de bring-up e regressão.

## Princípio (ADR-0007)

Inspirado no OpenCore: os binários da Apple ficam idênticos ao original e toda adaptação fica **abaixo** do sistema (loader, contratos de plataforma, drivers da camada de plataforma do XNU e patches documentados do XNU). **Nada da Apple está neste repositório**: cada usuário traz o próprio IPSW.

## Estado (v0.1.0)

- Kernel XNU 11417.140.69 (build DEV) no QEMU com B3/B4 verificados e GICv3/SGI1 da P0.12 verificados em uma CPU; o kernel entrou em bsd_init, sem concluí-lo.
- Próximos bloqueios independentes: **B6 IOPMGR** e **B7 provider pthread**. Não há userland funcional.
- Moto G53: o bootloader aceita `fastboot boot` (imagem Android do próprio aparelho). O XNU ainda **não** rodou no aparelho.
- Nada disso é compatibilidade de segurança Apple; os critérios D1–D4 (SpringBoard, Metal, toque) ainda não foram atingidos.

Detalhes no [STATUS.md](../../STATUS.md) (gerado automaticamente) e no [ROADMAP.md](../../ROADMAP.md).

## Como contribuir

A maior parte do trabalho útil não exige telefone nem arquivos da Apple. Leia o [CONTRIBUTING.md](../../CONTRIBUTING.md); agentes de IA começam pelo [AGENTS.md](../../AGENTS.md). Commits exigem DCO (`git commit -s`). Vulnerabilidades: reporte de forma privada pela aba **Security** do GitHub ([SECURITY.md](../../SECURITY.md)).

## Licença

Código e documentação do projeto: Apache-2.0. Modificações do XNU: APSL-2.0. Dependências mantêm suas licenças.
