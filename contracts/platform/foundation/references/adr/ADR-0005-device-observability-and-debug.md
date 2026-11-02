# ADR-0005 — Observabilidade e depuração no Moto G53 (penang) sem gravação de partições
Data: 2026-10-28 · Status: **PROPOSTO** (aguarda aceite do responsável) · Complementa ADR-0001 e ADR-0004.

## Contexto
- Produção Qualcomm: JTAG presumivelmente desabilitado por fusível (INFERRED, não medido); não há depurador de hardware a contar.
- Kernel entra em EL1 sob o hipervisor Qualcomm (carve-out `hyp@80000000`, MEASURED); PSCI declarado `smc` no DT base (E2).
- O `xnuloader` do HackiOS `d82c64a` já implementa supervisor em núcleo separado, heartbeat, watchdog do hipervisor, canal USB fastboot e log em `logdump` (E2, leitura de código; ver `reports/M3-LEAD-REVIEW-2026-10-28.md` §3). Provado pelo upstream só no redwood (SM7325).
- Não existe, que saibamos, porte do Mu-Silicium para holi/SM4350 (UNKNOWN; bloqueador do ADR-0001).

## Decisão
### 1. Política de não-gravação
Durante todo o bring-up o aparelho só recebe imagens via **`fastboot boot`** (carregadas em RAM). Nenhum `fastboot flash`, `erase`, `set_active` ou escrita em partição. Um ciclo de energia sempre devolve o Android stock. Gravar qualquer coisa exige ADR próprio, backup verificado e, se ocorrer, apenas no slot inativo.
O penang **não tem** partição `logdump` (D0, MEASURED: há `logfs` e `apdp`, que não serão tocadas). O loader roda com escrita em armazenamento desligada e o log sai **só por USB** (C2) ou pela tela (C0/C4). Candidatos adicionais a testar como hipótese: região `ramoops_mem@0xaf000000` (log que sobreviveria a reset quente) e Coresight/ETM.

### 2. Canais de observação, do mais precoce ao mais rico
| Camada | Canal | Funciona quando | Status |
|---|---|---|---|
| C0 | Tela via GOP: barras de cor por estágio (loader e primeiras instruções do kernel escrevem cores fixas no framebuffer) | Até sem USB e sem texto; registrável por câmera | A implementar |
| C1 | Console do loader na tela (GOP; no stock o splash fica em `0x85200000`, 12 MiB) + log em RAM | UEFI vivo | Upstream |
| C2 | USB fastboot do loader: `fastboot get_staged` traz o log; host usa `xl` | Núcleo supervisor vivo | Upstream, portar |
| C3 | Mailbox compartilhada: heartbeat + código de estágio + vetor de exceção mínimo que grava ESR/ELR/FAR/SPSR e para o núcleo | Kernel em outro núcleo, mesmo travado | Heartbeat upstream; vetor de exceção a implementar |
| C4 | Console de vídeo do XNU (`boot_args.Video` = GOP, boot-arg `-v`) + `kprintf` para o buffer de log | Após `arm_init` | Upstream (8796) |
| C5 | Retorno do kernel ao loader (`loader_return.c`) com log, motivo e estado | Kernel saudável o bastante para voltar | Upstream (8796) |
| C6 | UART física | — | **Descartada** pelo responsável em 2026-10-28 (sem ferramental para abrir o aparelho). Não planejar em torno dela. |

### 3. Depuração
- **No host (principal):** QEMU com `-s -S`, `gdb-multiarch` ou `lldb` (gdb-remote) com símbolos do kernel DEVELOPMENT e as `lldbmacros` do próprio XNU; `-d int,guest_errors,unimp` para pegar registradores de sistema Apple não implementados em Cortex. Todo bug do aparelho deve virar um caso reproduzível num perfil QEMU com o mapa de memória e o DT do penang antes de ser corrigido.
- **No aparelho:** sem depurador ao vivo. Método = bissecção por marcadores (C0/C3), `CHECKPOINT_SELFTEST`, e dump de exceção pelo supervisor. Cada execução gera um artefato de log com hash, imagem usada e boot-args.

### 4. Sequência de gates no aparelho (cada um com aprovação humana)
- **D0** só leitura: `fastboot getvar all`, lista de partições por `adb` (root), confirmar backups existentes.
- **D1** `fastboot boot` de um `boot.img` **do próprio stock** (zero novidade) → prova o boot em RAM e o caminho de volta.
- **D2** `fastboot boot` de UEFI holi (porte nosso) até GOP na tela; nada além disso.
- **D3** + loader em modo `usb`: log via `fastboot get_staged`; medir o watchdog (tempo até mordida) sem kernel.
- **D4** `CHECKPOINT_SELFTEST`: caminho de retorno sem kernel.
- **D5** payload sintético M3 no núcleo 1 via PSCI `smc` → heartbeat → retorno; depois travamento proposital → `OTHER_CPU_HUNG` com log.
- **D6** XNU 8796 (Trilho A) até o primeiro marcador; depois `arm_init`, `bsd_init`.

Recuperação em qualquer gate: segurar Power + Volume − até reiniciar (reset do PMIC; tempo exato a medir no D1). Bateria acima de 50 % antes de cada sessão: no UEFI o carregamento depende da pilha de carregador da Qualcomm.

## Consequências
- O bloqueador real do aparelho passa a ser o **porte do UEFI para holi** (BootShim + drivers do `xbl` do próprio ROM stock + mapa de memória), não o XNU.
- Nada do que roda no telefone é permanente; o risco residual é elétrico/térmico e de watchdog, não de brick por gravação.

## Critério de reabertura
`fastboot boot` recusado pelo ABL do penang (D0/D1); ausência de USB device protocol utilizável no UEFI holi; ou evidência de que o hipervisor bloqueia `CPU_ON` para o segundo núcleo.
