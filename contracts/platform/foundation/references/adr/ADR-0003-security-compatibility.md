# ADR-0003 — Segurança da camada de compatibilidade XNU–Qualcomm
Data: 2026-10-24 | Status: **PROPOSED; NO HARDWARE SECURITY GUARANTEE**
Autores: equipe do laboratório; revisão crítica Claude Code recebida e triada em 2026-10-24 (ver docs/research/CLAUDE-REVIEW-TRIAGE.md).

## Pergunta
É possível oferecer iOS18 com binários Apple originais e segurança conservada em hardware Snapdragon sem Apple BootROM/SEP/SPTM?

## Decisão
**Separar compatibilidade funcional da segurança provada, e implementar uma raiz de confiança própria quando o hardware permitir, sem simular a identidade da Apple.**
1. Artefatos Apple: originais imutáveis em armazenamento privado de desenvolvimento (SHA-256 de cada imagem, CodeDirectory, build, versão). A assinatura só será anunciada como verificada quando existir uma cadeia de certificação/manifesto válida para o artefato; em outros casos, reportar apenas hash e procedência. O ticket personalizado TSS/ECID da Apple não é reproduzido no Qualcomm. Não publicar IPSW, firmware Apple ou Qualcomm.
2. A execução de binários por adaptação em memória, tradução, interceptação de chamadas ou patch tem classificação própria. Registrar original e material executado, delta e política de segurança afetada. Não usar 'unmodified execution' se bytes ou contratos/semântica de execução forem alterados por patch, tradução ou emulação; distinguir original em disco, runtime transformado e ambiente virtualizado.
3. Carregador: separar autenticação de artefatos com chave da plataforma e verificação das assinaturas Apple. Garantir fail-closed para itens alterados ou perfis não reconhecidos; manter logs mensuráveis e recuperação Android.
4. Memória: estudar monitor EL2/S2 para isolamento e 16→4 KiB, mas **somente** após prova de EL2, VHE/NV/NV2 e suporte real dos núcleos Kryo 460. Sem NV2, reprojetar kernel port ou ABI; não assumir emulação transparente do stock kernelcache.
5. Assinatura de código/AMFI/entitlements/sandbox/TCC: não utilizar `allow all` como política de release; só marcar funcional após testes negativos comprovarem rejeição de código inválido e ausência de escalada.
6. Criptografia de dados: priorizar mecanismos com chaves novas protegidas pelo TEE Qualcomm quando acessíveis, vinculadas ao estado de boot e resistentes a rollback, com avaliação de dependência de firmware proprietário. Não fingir UID/SEP nem reutilizar chaves do Android.
7. Projeto de updates: assinaturas próprias, slots/versões imutáveis quando o armazenamento permitir; A/B e rollback transacional sujeito a ensaio físico; no primeiro ciclo só QEMU. Sem OTA oficial da Apple.
8. UEFI/Vault/AVB: considerar mecanismos OpenCore e AVB como padrões arquiteturais, não configuração transportável byte a byte.

## Contratos de segurança e suas garantias
| Contrato | Impl. plausível | Requer para anunciar | Limitação explícita |
|---|---|---|---|
| `SOURCE_INTEGRITY` | SHA-256 e, quando possível, validação criptográfica do artefato original | distinguir hash, CodeDirectory e assinatura/ticket personalizados | não implica confiança da plataforma nem Apple BootROM |
| `PROJECT_BOOT_INTEGRITY` | assinatura do loader/manifesto | hardware-root-of-trust verificado ou declarar software-only | Android unlocked enfraquece attestation |
| `CODE_SIGNING` | regras por CodeDirectory e policy engine | testes positivos/negativos em runtime | patches in-memory mudam o código executado |
| `PAGE_ISOLATION` | EL2 stage-2/shadow + policy | testes de leitura/escrita/W^X, DMA, TOCTOU | não equivale a Apple SPTM/GXF |
| `PAC_COMPAT` | tradução controlada/monitor | execução + verificação de integridade real, sem NOP indiscriminado | sem hardware PAC não promete o mesmo CFI |
| `KEY_PROTECTION` | TEE KeyMint/TA disponível | prova de chaves não extraíveis, política boot | NÃO é Secure Enclave Apple |
| `DATA_ENCRYPTION` | chaves próprias + volume cifrado | lock-state, replay/rollback e recuperação testados | sem Apple Data Protection classes nativas |
| `UPDATE_ROLLBACK` | staging/snapshots gerenciados | recovery testado em duas revisões de firmware | sem Apple OTA |
| `APPLE_SERVICES` | não prometido | prova de suporte lícito, sem falsas identidades | Secure Enclave/attestation Apple ausentes |

## Proibições de linguagem na documentação
- Não dizer `equivalente ao Secure Enclave`, `Apple Secure Boot completo`, `Apple hardware-backed`, `protegido pelo SPTM da Apple`, `iOS íntegro ponta a ponta` para o G53.
- Não confundir validar assinatura da Apple com executar em cadeia validada desde BootROM da Apple.
- Um monitor que **responde** a chamadas de LOCKDOWN não prova enforcement de imutabilidade.
- Não afirmar que iOS 18 roda na GPU Adreno ou que usuário pode armazenar dados confidenciais até que os controles sejam medidos.

## Critérios de passagem para pesquisa prática
S1. Inventário estático e relatório de lacunas de EL2, PAC, NV2 e granule no penang. O inventário não comprova EL2 acessível; prova dinâmica em hardware requer aprovação separada e plano seguro. UNKNOWN não vira TRUE/FALSE por inferência.
S2. Em QEMU, máquina host testa corrupção intencional de arquivos e rejeição de artefatos ou assinaturas inválidas.
S3. Em QEMU, monitor testa permissões de tabelas, revogação W^X e acesso negado a memória fora dos mapeamentos.
S4. Em QEMU, simulador de falhas de energia valida transação sem corromper dados de usuário.
S5. Em hardware, somente após backup, política de recuperação, observabilidade e revisão autorizada; testes negativos não devem tocar a instalação Android.

## Referências
- Ver `docs/research/SECURITY-REFERENCE-MATRIX.md` (fontes com link e commit quando aplicável).
- Status: `manifests/security-assurance.json` é fonte única de afirmações verificadas, propostas e bloqueadas.
