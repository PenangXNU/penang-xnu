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

## Progresso de pesquisa posterior à v0.1.0 (04/11/2026)

O estado v0.1.0 acima é um snapshot histórico gerado e permanece inalterado. Em worktrees locais isoladas, foram concluídas a P0.13 (provider pthread), a P0.14 (AES-CBC real e retorno de `tcp_init`), a P0.15 (`CCRNG_RUNTIME_VERIFIED`, duas solicitações originais de 32 bytes atendidas pelo HMAC_DRBG e retorno de `necp_init` no QEMU), e a M10.2 (21/21 testes M10, 23/23 cenários QEMU e suíte host geral verde).

O próximo bloqueio **emulado** é `B10_BSD_ROOT_MEDIA_WAIT`: `IOFindBSDRoot` aguarda `IOMedia / Apple_HFS`; a observação não comprova deadlock permanente. `B6_IOPMGR` segue preservado e não implementado. Os checkers históricos da P0.10 que exigiam panic permanecem FAIL nesse critério; a progressão P0.15 foi validada separadamente.

**No telefone:** Stage-0 próprio e XNU ainda **não foram executados**. M10.2 terminou em `PHYSICAL_HOLD` por pendências de framebuffer, recuperação independente e aceitação do payload pelo ABL. Não há root filesystem, userland, SpringBoard, Metal ou segurança Apple equivalente demonstrados.

Referências de pesquisa local, apenas para proveniência: P0.13 `90a3f09`, P0.14 `f72dcfc`, P0.15 `8a75e40`, M10.2 `743a977`. Nenhum firmware, binário Apple, dump de aparelho, chave ou log bruto foi publicado. O código experimental do kernel permanece candidato a uma futura v0.2 após auditoria específica.

## Como contribuir

A maior parte do trabalho útil não exige telefone nem arquivos da Apple. Leia o [CONTRIBUTING.md](../../CONTRIBUTING.md); agentes de IA começam pelo [AGENTS.md](../../AGENTS.md). Commits exigem DCO (`git commit -s`). Vulnerabilidades: reporte de forma privada pela aba **Security** do GitHub ([SECURITY.md](../../SECURITY.md)).

## Licença

Código e documentação do projeto: Apache-2.0. Modificações do XNU: APSL-2.0. Dependências mantêm suas licenças.
