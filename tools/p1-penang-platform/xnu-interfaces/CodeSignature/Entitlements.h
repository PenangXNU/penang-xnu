/* SPDX-License-Identifier: Apache-2.0 */
/*
 * CANDIDATE interface, not Apple's header and not approved for integration.
 *
 * XNU 11417.140.69 includes <CodeSignature/Entitlements.h> from
 * bsd/net/necp_client.c and bsd/kern/kern_exec.c. The header is not part of
 * the public source; upstream resolves it from the Kernel Development Kit,
 * which this project does not use. The only identifiers those two files take
 * from it are the four macros below, each passed as a `const char *`
 * entitlement key to IOTaskHasEntitlement() / IOVnodeHasEntitlement().
 *
 * Names: dictated by the public consumers. Values: NOT recoverable from
 * public source. The public XNU tests (tests/entitlements/
 * hardened-binary-entitlements-{1,2}.plist) grant two to four candidate keys
 * for each macro and do not say which single key the macro holds. This file
 * binds each name to the candidate that Apple documents publicly for
 * third-party browser engines (BrowserEngineKit). That is a project policy
 * choice among evidenced candidates; a kernel built with it may treat
 * binaries differently from Apple's wherever Apple's header names another
 * candidate. It grants nothing by itself: entitlement validation and trust
 * admission are separate and remain OPEN.
 */
#ifndef PENANG_CANDIDATE_CODESIGNATURE_ENTITLEMENTS_H
#define PENANG_CANDIDATE_CODESIGNATURE_ENTITLEMENTS_H

#define PENANG_CODESIGNATURE_ENTITLEMENTS_CANDIDATE 1

#define kCSWebBrowserHostEntitlement       "com.apple.developer.web-browser-engine.host"
#define kCSWebBrowserGPUEntitlement        "com.apple.developer.web-browser-engine.rendering"
#define kCSWebBrowserNetworkEntitlement    "com.apple.developer.web-browser-engine.networking"
#define kCSWebBrowserWebContentEntitlement "com.apple.developer.web-browser-engine.webcontent"

#endif /* PENANG_CANDIDATE_CODESIGNATURE_ENTITLEMENTS_H */
