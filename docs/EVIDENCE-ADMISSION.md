# Admissão de evidência e uso do validador

Este incremento verifica consistência documental offline. Não é uma revisão de implementação XNU, certificação de segurança, assinatura de proveniência ou prova nova de runtime.

## Entradas e resultados

- manifests/target-contract.json fixa o alvo e aponta para ADRs e critérios aceitos.
- manifests/evidence-index.json separa tarefa, gate, ambiente, qualidade, funcionalidade e limites.
- evidence/source-register.json conserva metadados de origem, commit e SHA, fatos selecionados e registros admitidos.
- foundation.lock.json fixa as projeções selecionadas, regras, schemas e cópias originais das ADRs.
- manifests/derivation-rules.json define a agregação: BLOCKED e FAIL prevalecem; prova conhecida misturada com UNKNOWN produz PARTIAL.
- project-status.json, STATUS.md e EVIDENCE-INDEX.md são resultados determinísticos. PASS de uma tarefa host não satisfaz D1–D4.

A verificação padrão é somente leitura. O modo --write escreve apenas esses três resultados e recusa entradas inválidas. A suíte cria e remove fixtures sintéticas dentro de reports/test-tmp, no workspace pai.

## Atualizar uma fonte

Um colaborador apresenta a fonte real, revisão, hashes, ambiente, método, resultado e limites. O revisor distingue o que é decisão do responsável, observação, relato ou hipótese.
Depois da revisão, atualizam-se a projeção admitida, suas referências, os schemas/regras quando necessário e os hashes no lock; em seguida, regenera-se o status e executa-se a suíte.

Não editar o registro admitido e o lock para fabricar confirmação: ambos são arquivos locais, sem raiz criptográfica independente. O lock detecta deriva acidental e modificações isoladas; não impede adulteração coordenada, inclusive do próprio validador. A revisão humana da evidência continua necessária. Não há limite artificial de fontes, pesquisas ou ADRs.

## Public preview

O snapshot W1 está em contracts/platform/foundation. O frontend público reutiliza o núcleo documental sem modificar seus algoritmos.
Use python3 -B scripts/validate_public.py na raiz pública; o exemplo sintético e a CLI estão documentados em docs/VALIDATION.md.
O launcher histórico validate-foundation.py e o modo com índice privado não integram esta árvore. Não é necessário obter originais privados.
Os caminhos de manifestos citados acima são relativos ao snapshot W1, não à raiz do preview.

## Alcance

Links locais e âncoras Markdown/JSON são conferidos, inclusive referências relativas que permanecem dentro da árvore. Links externos não são consultados. Referências externas de schemas são recusadas sem acesso à rede.
A fronteira pública verifica tipos de arquivo selecionados e padrões conhecidos de dados privados; não é scan exaustivo de segredos, licenças ou histórico Git. PUBLICATION_BLOCKED continua vigente.
