# W4 — candidata pública pronta para revisão

**GO para revisão independente e aceite humano. Publicação permanece bloqueada.**
Diretório/repositório novo: Penang-XNU-Public-Preview.
Branch local: public-preview/w4-rc1.
Snapshot fonte: 1f9c434e9a396bd3c1ef58261d5051a716236766.
Commit de fechamento: identificado pelo histórico Git que contém esta revisão, sem hash autorreferencial.

## Entrega concreta

127 arquivos versionados, com código C/Rust/Python funcional, perfis QEMU/Holi/Trinket, schemas, exemplo sintético, testes e documentação.
45 entradas selecionadas individualmente de W1/W2/W3; 24 dependências externas inventariadas; 46 notices originais preservados byte a byte.
Nenhum banco de objetos ou commit original foi importado. Não há remote configurado neste repositório.

[README](README.md), [contrato](contracts/platform/CONTRACT.md), [validação](docs/VALIDATION.md), [licenciamento](docs/LICENSING.md), [autoria](AUTHORS.md), [roadmap](ROADMAP.md) e [issues iniciais](docs/INITIAL-ISSUES.md).
A licença proposta para material próprio é MIT; sua adoção final depende do responsável. Notices/dependências preservam os termos originais.

Inventários públicos: [seleção e adaptações](audit/selected-sources.json), [licenças por arquivo](audit/file-licenses.json), [dependências](audit/dependencies.json), [fronteira de arquivos](audit/public-files.json) e [exclusões](audit/exclusions.json).
O inventário completo dos nomes excluídos está disponível localmente em .build/review/excluded-files.json, fora da árvore pública proposta.
Os artefatos e logs de revisão em .build são ignorados; não devem integrar um upload da candidata. Os 127 arquivos do manifesto/versionamento definem a proposta pública.

## Alterações de empacotamento

Algoritmos C/Rust e núcleo documental W1 preservados. ADR-0004/0006 mantidas byte a byte e aceitas.
Removido apenas o metadado desnecessário da condição de display do aparelho; status derivado regenerado pelo núcleo W1 inalterado.
Admissões apontam para projeções públicas explícitas em vez de patch XNU, relatório de runtime completo ou inventário bruto.
O teste negativo de alteração de bytes conserva as mesmas asserções e muda um caminho de fixture.
Documentação W1 recebeu contexto histórico/caminhos públicos. Não houve relaxamento de gate, mudança de arquitetura ou qualificação automática.
Patches e helpers dependentes da fonte XNU foram excluídos; não foram relicenciados nem contados como testes executados aqui.

A primeira rodada teve 34 falhas sintéticas por input_sha256 derivado desatualizado após a redação do metadado.
A correção regenerou somente esse campo por meio da função W1 original. O resultado FAIL e o hash do log permanecem no gate; nenhuma falha foi apagada ou mascarada.

## Verificação

| Gate | Resultado e escopo |
|---|---|
| G0 | PASS — quatro checkouts originais, W1 e main preservados |
| G1 | PASS — inventário/licenças/notices para revisão da candidata; adoção MIT e atribuição final ainda humanas |
| G2 | PASS — nenhum conteúdo privado/proprietário ou segredo identificado nos arquivos/objetos verificados; padrões limitados, revisão independente pendente |
| G3 | PASS — clone local limpo: 36 sintéticos + 8 ABI/compilação/fronteira + 6 Rust = 50 testes; build e Clippy PASS |
| G4 | PASS — README, contribuição, segurança e estados técnicos coerentes com fontes e limites |
| G5 | PASS — pronta para revisão independente; não significa revisão independente aprovada |

O clone recebeu os 127 arquivos versionados byte a byte, sem .build ou entradas do laboratório, e usou um target de build novo.
Foram reutilizados apenas cache público Cargo e pacotes Python do host; isso não equivale a uma segunda máquina provisionada independentemente.
Quatro objetos ARM64 próprios compilados; consumidor ABI C/C++ executado no host. Guarda EL1 não executada.
Os negativos da fronteira cobrem token sintético, caminho privado, arquivo de imagem/arquivo compactado, symlink e segredo apagado no histórico Git.
Comandos, casos, hashes de logs e procedência: [W4-PUBLICATION-GATES.json](W4-PUBLICATION-GATES.json).
Evidência local ignorada: .build/w4/ e .build/review/; recibo final .build/review/final-receipt.json.

## Estado e limites de publicação

W1 é fundação documental aceita; W2/W3 é HOST_TESTED/compilação ARM64.
P0.9 registra criptografia integrada no QEMU DEV: referência documental apenas, com hashes, sem importação ou reexecução.
B3 permanece BLOCKED; D1–D4 não demonstrados; Sofiar P1/UNKNOWN.
Nenhuma equivalência de segurança Apple, boot completo, execução EL1, userland ou GPU física é concedida.
ROM/IPSW/KDK/firmware, dumps, logs brutos privados, credenciais, configurações de agentes e checkpoints internos estão excluídos.
Nenhum push, criação remota, visibilidade, CI remoto, QEMU novo ou operação no telefone.

## Handoff para o responsável

1. Revisar seleção, atribuição e a proposta MIT; os autores históricos do BSP não podem ser identificados pelos placeholders Git.
2. Solicitar revisão independente dos fontes, ABI, licenças, limites e testes.
3. Definir canal confidencial de segurança e decidir explicitamente pelo aceite da candidata.
4. Somente uma instrução posterior pode autorizar publicação ou ações remotas.

Nenhum novo trabalho foi iniciado automaticamente. PUBLICATION_BLOCKED permanece até autorização explícita.
