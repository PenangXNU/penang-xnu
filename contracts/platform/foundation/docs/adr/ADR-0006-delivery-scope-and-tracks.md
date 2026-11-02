# ADR-0006 — Escopo da entrega, trilhos de kernel e equipe
Data: 2026-10-28 · Status: **ACEITO** (decisões explícitas do responsável em 2026-10-28) · Refina ADR-0004; não o revoga.

## Decisões do responsável
1. **Entrega = iPadOS 18.7.8 (22H352, iPad7,11) rodando no penang.** O iOS 16.5 não é entrega.
2. **XNU 8796 + patches HackiOS é andaime interno**: serve só para validar UEFI, loader, mapa de memória, PSCI/SMP, GIC, timer, display e o canal de debug no penang. Nenhum artefato do 8796 é entregue nem anunciado.
3. **Metal na GPU é obrigatório** para considerar a entrega cumprida. Renderização por software pode existir apenas como etapa interna de diagnóstico.
4. **Sem UART física**; a observabilidade segue o ADR-0005 (tela, USB do loader, mailbox em RAM).
5. **Equipe:** Claude Opus 5.5 lidera, integra e revisa; GPT 6.1 Sol High implementa tarefas fechadas; Gemini 4 Argon High revisa plataforma/segurança. Gemini 3.8 Flash e GPT-6 pausados nesta fase.

## Definição de "entregue" (todos verificáveis, com log e hash)
- D-1 Kernel `xnu-11417.140.69` com config genérica ARMv8.2 (ADR-0004 Trilho B) inicia no penang via `fastboot boot`, sem gravação.
- D-2 `launchd` do iPadOS 18.7.8 ORIGINAL (binários Apple sem alteração de byte, classificação do ADR-0003) chega a `SpringBoard`.
- D-3 `MTLCreateSystemDefaultDevice()` retorna um dispositivo backed pelo Adreno 619 e o SpringBoard compõe via Metal/CoreAnimation acelerado; medido por contadores da GPU, não por inspeção visual.
- D-4 Toque funcional e retorno ao Android por reinício.

## Consequências (honestas)
- O caminho crítico passa a ter **quatro frentes longas**: (a) porte do UEFI para holi; (b) config genérica ARMv8.2 do 11417 + delta BCM2837 portado; (c) driver de kernel do Adreno 619 em IOKit; (d) implementação de Metal sobre o Adreno (o upstream HackiOS usa Mesa/freedreno para OpenGL ES e descreve Metal como "em andamento" no Adreno 642L — E1, README do HackiOS `d82c64a`).
- A frente (d) é a de maior incerteza técnica do projeto. Antes de comprometer prazo, faz-se um estudo de viabilidade (fora do caminho do aparelho): quais serviços/IOKit o `Metal.framework` do iPadOS 18.7.8 espera do driver (`AGXMetal*` do A10) e o que o HackiOS/hackios-xnu-userland já resolveu.
- A10 (T8010) não tem PAC, o que mantém o userland ORIGINAL viável (F07); mas o driver Metal do iPad7,11 é `AGX` específico da GPU Apple e **não** roda no Adreno — o bundle de driver Metal será código nosso (RUNTIME_ADAPTED do lado do sistema, nunca alteração dos binários Apple).

## Ordem de trabalho
M3b supervisor SMP (host) → M4 andaime 8796 em QEMU → porte UEFI holi → D0–D6 no aparelho com o andaime → Trilho B 11417 em QEMU → 11417 no aparelho → userland 18.7.8 → driver Adreno → Metal. Estudo de viabilidade do Metal roda em paralelo desde já.
