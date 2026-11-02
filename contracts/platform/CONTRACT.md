# Contrato de plataforma v1 — recorte CPU/memória

A ABI C é PV_ABI_VERSION=0x00010000. Callers emprestam snapshots, requests e arrays durante a chamada; o decoder não retém ponteiros, aloca memória ou faz MMIO.
Resultados distinguem PASS, FAIL, BLOCKED e NOT_RUN, com código de causa. Valores desconhecidos não são tratados como suporte.

## Tradução

O recorte segue a configuração QEMU_VIRT da P0.7: páginas 16 KiB, PA 40 bits e VA 47 bits.
TCR_EL1_BOOT original avaliado é 0x226511a511. Atributos normais WB/outer-shareable são preservados.
A variante ASID16 só passa com o campo arquitetural correspondente; flags fora do contrato são recusadas.
TGran16=0 significa ausência; 1 e 2 indicam suporte no modo tradicional validado; codificações reservadas permanecem UNKNOWN/BLOCKED.
PARange reservado, EL diferente de EL1, MMU já ligada no ponto pre-MMU, CPU diferente do A76 selecionado, TG/VA/IPS/atributos incompatíveis falham ou bloqueiam.
O identificador de contexto virtual é fornecido pelo backend selecionado, não inferido de MIDR nem confundido com CPU física do host.

## Memória e reservas

pv_region usa intervalos [base,base+size), uint64, alinhados à página:
kind=1 RAM, kind=2 kernel, kind=3 dados/buffers de boot, kind=4 reserva adicional.
v1 exige uma janela RAM, uma reserva de kernel e uma de boot, com tamanho não zero, sem overflow e dentro do limite PA.
Reservas permanecem contidas na RAM e não se sobrepõem. O decoder não inventa a janela: o loader/observação deve fornecê-la.
Isso complementa os contratos de boot_args/ADT do loader existente, sem substituir reservas reais UEFI ou qualificação de RAM física.

## Perfis e utilização

QEMU_VIRT: ABI e recursos de referência extraídos do patch/loader existente; inicialização histórica até B3 separada de execução atual.
Holi: reutilização do validador BSP legado e dos valores auditados de inventário, sem promoção para driver XNU.
Trinket: declaração P1, identidade e recursos não medidos UNKNOWN; nenhum método operacional falsificado.

Cada capacidade tem SUPPORTED/UNSUPPORTED/UNKNOWN, origem, escopo, referência e enabled.
SUPPORTED em STATIC_CONTRACT ou ANDROID_ONLY não significa operacional em XNU. Este incremento não habilita capacidades por JSON.
O núcleo confere a compatibilidade perfil/backend, campos obrigatórios, bytes de fontes e admissões, e compara o descritor GIC Holi com o inventário.
Schemas, admissões e suas revisões seguem o formato de lock W1, sem substituir os três manifestos do alvo.
Detalhes, comandos e limites em tools/penang-validate/USAGE.md.
