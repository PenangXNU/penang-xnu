> W1 specification snapshot. The working CPU/memory subset is documented in contracts/platform/CONTRACT.md; this historical specification is not a claim that all interfaces are implemented.

# Contratos de plataforma — proposta de engenharia

Esta especificação não implementa os backends ou providers. Cada contrato deve identificar versão/ABI, funções reais, ownership/lifetime, memória, ordem, erros, capacidade, ambiente, fontes e negativos.

| Interface | Entrada/saída e obrigação | Prova mínima |
|---|---|---|
| Capabilities | Valores medidos/declarados/UNKNOWN separados; source pin por propriedade | Fixture inválida rejeitada; observação apropriada ao alvo |
| Memória | Mapa utilizável, reservas, granule, permissões e DMA coerentes | Oráculo host + runtime do ambiente anunciado |
| CPU/interrupções | Features, GIC, timer, PSCI e estados de entrada explícitos | Chamadas/eventos observados, não presença de nome no DT |
| Board/energia/debug | Placa seleciona recursos; falha preserva diagnóstico e recuperação | Canal qualificado e limites térmicos/energia |
| Provider | ABI/registro, política, raiz/nonce, autenticação, ciclo de vida e falha fechada | Implementação licenciada completa no escopo e negativos reais |
| Adaptador de versão | Delta Mach/MIG/VM/IOKit isolado da placa | Casos próprios de ABI da revisão fixada |
| Gráficos | Recursos/descritores/comandos, memória/sync e semântica de shader | Oráculos host e resultado GPU independente |

Um backend declara unsupported/unknown com erro definido quando não satisfaz o contrato. Não fabricar capacidade, sucesso de linker ou autenticação.
A619 e a plataforma Sofiar declarada requerem perfis distintos. A existência de uma interface em dois diretórios não demonstra generalidade. Nenhum monitor, workaround de 16 KiB ou quirk de segurança foi implementado nesta W1.
