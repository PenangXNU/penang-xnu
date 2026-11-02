# W4 RC2 — candidata pronta para decisão humana

**Estado técnico pronto; HUMAN_DECISION_REQUIRED. PUBLICATION_BLOCKED.**
Repositório existente: Penang-XNU-Public-Preview; branch public-preview/w4-rc1.
Baseline preservada: cfefdaa2a0b0201ee6f0df54910ffe220cfc99d4.
Commits RC2 anteriores ao fechamento:
- 66ef63676bb7202e5e4cdfdf2539f9f81c37d322 — atualização de estado e controles.
- fc685d99940aea60e7a53baad5c125c47ea3bb5b — normalização da projeção própria para integridade no clone.
O commit de fechamento é identificado pelo histórico que contém este registro, sem hash autorreferencial.

## Estado técnico fixado

P0.10: afa7ef6acfb89bf5ae2f3d0c67e7a775096f39b0, exclusivamente referência documental.
P0.9: criptografia unificada no kernel DEV, regressão QEMU e segurança no escopo dos registros.
P0.10: B3_RESEARCH_PROVIDER_RUNTIME_VERIFIED sob provider/raiz de pesquisa independentes, trust cache PNRA-R1 autenticado, aceitação positiva e rejeição de assinatura não autorizada.
Isso não é APPLE_IPADOS_SECURITY_COMPATIBLE. Interface Image4 de pesquisa declarada v7, operações restritas, sem entitlement Apple, SEP ou userland completo.

Novo limite no snapshot: B4_IOKIT_PLATFORM_DRIVER.
Panic: Unable to find driver for this platform: "qemu,virt".
Matching IOKit alcançado; Platform Expert válido não inicializado.
G53 físico/D1–D4 não demonstrados; M9 host only; Sofiar P1/UNKNOWN; nenhum progresso GPU/SpringBoard inferido.
Commits posteriores da frente B4 não atualizam esta RC automaticamente; ela não interrompe nem importa essa frente.

## O que mudou

README/STATUS e textos correntes foram reconciliados com a P0.10 fixada.
Uma camada própria deriva manifests/current-status.json, STATUS.md e o bloco marcado do README a partir de fonte/schema/derivação com hashes.
W1/W2/W3, ADRs, código C/Rust qualificado e relatórios/recibos W4 históricos foram preservados.
Scanner passou a rejeitar também extensões de chaves/seeds e contêineres não publicáveis; negativo sintético observado RED depois GREEN.
A primeira comparação do clone parou antes dos testes por CRLF em JSON próprio; foi corrigida a normalização LF e regenerado o hash de entrada. O resultado inicial não foi apagado.

[Fontes e hashes P0.10](evidence/current-status-sources.json), [status derivado](manifests/current-status.json), [escopo P0.10/B4](docs/P0.10-STATUS.md), [autorrevisão focalizada](audit/rc2-focused-review.json).
Arquivos exatos alterados desde a baseline: changed_files_from_baseline em [W4-RC2-GATES.json](W4-RC2-GATES.json).
Inventário/licenças: audit/public-files.json e audit/file-licenses.json. São 139 arquivos propostos.
Nenhuma dependência ou código P0.10/Monocypher foi incorporado; 24 pacotes e 46 notices originais permanecem inalterados.

## Verificação e gates

Clone local corrigido: 139 arquivos idênticos byte a byte, sem ignorados ou entradas privadas, target de build novo.
61 testes PASS: 36 CLI sintéticos, 19 ABI/compilação/fronteira/status e 6 Rust.
Derivação documental, build Rust/C e Clippy PASS; quatro objetos ARM64 próprios compilados, ABI C/C++ executada no host.
O clone reutilizou cache público/pacotes do mesmo host; não é uma segunda máquina nem uma revisão independente.
Varredura de arquivos/objetos/histórico e controles negativos PASS, sem achados materiais conhecidos dentro dos padrões e da seleção revistos. Não é prova de ausência absoluta de segredos.

| Gate | Resultado |
|---|---|
| G0 | PASS — W4 original/recibos, W1 e originais preservados |
| G1 | PASS — estado P0.10/B4 e limites documentais corretos |
| G2 | PASS — proveniência, pins e derivação coerentes |
| G3 | HUMAN_DECISION_REQUIRED — revisão concluída; licença/titularidade/atribuição ainda humanas |
| G4 | PASS — validação pública reproduzida no clone |
| G5 | PASS — controles de publicação sem achados materiais conhecidos; escopo limitado explícito |
| G6 | HUMAN_DECISION_REQUIRED — candidata tecnicamente pronta, sem aprovação/publicação concedida |

O recibo final local em .build/rc2-review/final-receipt.json registra HEAD e scan posterior ao fechamento.
.build contém apenas evidências de revisão ignoradas e não integra a proposta pública. Recibos W4 anteriores não foram sobrescritos.

## Limites e decisão

[PUBLICATION-DECISIONS.md](PUBLICATION-DECISIONS.md) contém somente as decisões humanas pendentes: licença própria, atribuição/direitos BSP, canal confidencial e aceite/autorização de publicação.
MIT segue proposta, não aprovada. Nomes Git não provam titularidade. Nenhum contato foi inventado.
Monocypher 4.0.2 é referência externa P0.10; o aviso de 4.0.3 foi registrado com fontes primárias em [nota específica](docs/MONOCYPHER-REVIEW.md).
Impacto no código incorporado exige revisão técnica independente; não foi declarada vulnerabilidade, inocuidade ou auditoria por herança.

Esta é autorrevisão focalizada, não aprovação independente. Nenhum QEMU, CI remoto, telefone, firmware ou publicação foi executado.
Recomendação: GO para decisão humana; NO-GO para publicação até decisões e autorização explícitas. Nenhuma continuação automática.
