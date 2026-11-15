# Penang XNU — ponto de retomada (15/11/2026)

O repositório da organização é a **fonte canônica de código e navegação**. O histórico bruto de firmware, imagens, sondas físicas e logs permanece privado. A última versão documental `v0.1.0` mantém seu estado congelado; esta nota documenta a **pesquisa posterior**, sem fingir uma nova release.

## Já foi demonstrado

- **DEVICE_VERIFIED:** M11-D3A1 executou um Stage-0 próprio em RAM no Moto G53. Mediu EL1, suporte a grânulo de 16 KiB, linhas de cache de 64 bytes, header FDT válido, metadados de seed de 512 bytes, 21 reservas com `reg` e oito dinâmicas. Houve retorno ao Android com quatro hashes de partições preservados. **Não foi um boot XNU.**
- **HOST_ONLY:** M11-R2C/R3 analisaram ABL/XBL, display, watchdog e QHEE; B1.4-H1 confrontou FDTs e geometria da DRAM; B4.2 implementou contratos e guards de PMAP; B4.3A-R3 identificou interfaces SCM e uma chamada condicional do ABL após EBS. **Acesso/autorização ao XNU NS-EL1 e grants efetivos ainda não foram provados.**
- **QEMU_VERIFIED:** T72-Q5 executou dyld e main Mach-O próprios em EL0 sob XNU 11417/QEMU_VIRT; observou relocação efetiva, consumo de ponteiro e conclusão de fixture; 138 eventos, 34/34 verificações independentes. `TRUST_ADMISSION=OPEN` e nenhum iPadOS completo.
- **U2:** a identidade ADB do **G53 penang** foi recuperada. O Motorola `rav` é outro telefone e não deve ser confundido.

## Pendência atual

Há dez gates físicos abertos, com investigação extensa prévia: FDT dinâmica; ABI SCM XNU; Stage-2/XPU; DMA/SMMU; PMAP; compilação VM/PMAP; link XNU físico; GIC/timer; entropia e entrada do XNU no aparelho. O bloqueio imediato do target nativo é um **provider físico de memória/PMAP legítimo**, não falta de um levantamento inicial da ROM.

Próximo passo: [issue #4](https://github.com/PenangXNU/penang-xnu/issues/4) para integrar um provider físico legítimo; em paralelo, o fixture BSD `getpid` já compilado no host ainda precisa de execução real em QEMU. Não refazer as sondas ou os testes Q5 encerrados.

**Leia sempre:** [entrada canônica](../research/RESUME-HERE.md), [inventário de evidência física](../research/2026-11-15-physical-evidence-ledger.md), [Q5](../research/2026-11-15-q5-el0.md), [reconciliação 11/11](../research/2026-11-11-reconciliation.md). As fontes privadas e seus hashes ficam nos handoffs originais, não nesta árvore pública.

**Limite:** nenhuma publicação é autorização para flash, fastboot, reboot, escrita em partições ou sondagem de permissões protegidas. O ensaio XNU físico exige kernel nativo linkado, contrato de memória válido, diagnóstico/recuperação e consentimento específico.
