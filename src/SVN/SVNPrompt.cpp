// TortoiseSVN - a Windows shell extension for easy version control

// Copyright (C) 2003-2016 - TortoiseSVN

// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//
#include "stdafx.h"
#include "SVNPrompt.h"
#include "PromptDlg.h"
#include "SimplePrompt.h"
#include "Dlgs.h"
#include "registry.h"
#include "TortoiseProc.h"
#include "UnicodeUtils.h"
#include "AppUtils.h"
#include "StringUtils.h"
#include "TSVNAuth.h"
#include "SelectFileFilter.h"
#include "FormatMessageWrapper.h"
#include "SmartHandle.h"
#include "TempFile.h"
#include "PathUtils.h"

#define IDCUSTOM1           23
#define IDCUSTOM2           24
#define IDCUSTOM3           25

SVNPrompt::SVNPrompt(bool suppressUI)
{
    m_app = NULL;
    m_hParentWnd = NULL;
    m_bPromptShown = false;
    m_bSuppressed = suppressUI;
    auth_baton = NULL;
}

SVNPrompt::~SVNPrompt()
{
}

void SVNPrompt::Init(apr_pool_t *pool, svn_client_ctx_t* ctx)
{
    // set up authentication

    svn_auth_provider_object_t *provider;

    /* The whole list of registered providers */
    apr_array_header_t *providers = apr_array_make (pool, 14, sizeof (svn_auth_provider_object_t *));

    if (ctx->config != nullptr)
    {
        svn_config_t * cfg_config = (svn_config_t *)apr_hash_get(ctx->config, SVN_CONFIG_CATEGORY_CONFIG, APR_HASH_KEY_STRING);
        if (cfg_config != nullptr)
        {
            /* Populate the registered providers with the platform-specific providers */
            svn_auth_get_platform_specific_client_providers(&providers, cfg_config, pool);
        }
    }

    svn_auth_get_tsvn_simple_provider (&provider, pool);
    APR_ARRAY_PUSH (providers, svn_auth_provider_object_t *) = provider;
    /* The main disk-caching auth providers, for both
    'username/password' creds and 'username' creds.  */
    svn_auth_get_simple_provider2 (&provider, svn_auth_plaintext_prompt, this, pool);
    APR_ARRAY_PUSH (providers, svn_auth_provider_object_t *) = provider;
    svn_auth_get_username_provider (&provider, pool);
    APR_ARRAY_PUSH (providers, svn_auth_provider_object_t *) = provider;

    // The server-cert, client-cert, and client-cert-password providers.
    svn_auth_get_platform_specific_provider(&provider, "windows", "ssl_server_trust", pool);
    if (provider)
        APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;
    // The windows ssl authority certificate CRYPTOAPI provider.
    svn_auth_get_platform_specific_provider(&provider, "windows", "ssl_server_authority", pool);
    if (provider)
        APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;
    svn_auth_get_ssl_server_trust_file_provider(&provider, pool);
    APR_ARRAY_PUSH (providers, svn_auth_provider_object_t *) = provider;
    svn_auth_get_ssl_client_cert_file_provider (&provider, pool);
    APR_ARRAY_PUSH (providers, svn_auth_provider_object_t *) = provider;
    svn_auth_get_ssl_client_cert_pw_file_provider2 (&provider, svn_auth_plaintext_passphrase_prompt, this, pool);
    APR_ARRAY_PUSH (providers, svn_auth_provider_object_t *) = provider;

    /* Two prompting providers, one for username/password, one for
    just username. */
    svn_auth_get_simple_prompt_provider (&provider, (svn_auth_simple_prompt_func_t)simpleprompt, this, 3, /* retry limit */ pool);
    APR_ARRAY_PUSH (providers, svn_auth_provider_object_t *) = provider;
    svn_auth_get_username_prompt_provider (&provider, (svn_auth_username_prompt_func_t)userprompt, this, 3, /* retry limit */ pool);
    APR_ARRAY_PUSH (providers, svn_auth_provider_object_t *) = provider;

    /* Three prompting providers for server-certs, client-certs,
    and client-cert-passphrases.  */
    svn_auth_get_ssl_server_trust_prompt_provider (&provider, sslserverprompt, this, pool);
    APR_ARRAY_PUSH (providers, svn_auth_provider_object_t *) = provider;
    svn_auth_get_ssl_client_cert_prompt_provider (&provider, sslclientprompt, this, 2, pool);
    APR_ARRAY_PUSH (providers, svn_auth_provider_object_t *) = provider;
    svn_auth_get_ssl_client_cert_pw_prompt_provider (&provider, sslpwprompt, this, 2, pool);
    APR_ARRAY_PUSH (providers, svn_auth_provider_object_t *) = provider;

    /* Build an authentication baton to give to libsvn_client. */
    svn_auth_open (&auth_baton, providers, pool);
    ctx->auth_baton = auth_baton;
}

BOOL SVNPrompt::Prompt(CString& info, BOOL hide, CString promptphrase, BOOL& may_save)
{
    CPromptDlg dlg(CWnd::FromHandle(FindParentWindow(m_hParentWnd)));
    dlg.SetHide(hide);
    dlg.m_info = promptphrase;
    INT_PTR nResponse = dlg.DoModal();
    m_bPromptShown = true;
    if (nResponse == IDOK)
    {
        info = dlg.m_sPass;
        may_save = dlg.m_saveCheck;
        if (m_app)
            m_app->DoWaitCursor(0);
        return TRUE;
    }
    if (nResponse == IDABORT)
    {
        //the prompt dialog box could not be shown!
        ShowErrorMessage();
    }
    return FALSE;
}

BOOL SVNPrompt::SimplePrompt(CString& username, CString& password, const CString& Realm, BOOL& may_save)
{
    CSimplePrompt dlg(CWnd::FromHandle(FindParentWindow(m_hParentWnd)));
    dlg.m_sRealm = Realm;
    INT_PTR nResponse = dlg.DoModal();
    m_bPromptShown = true;
    if (nResponse == IDOK)
    {
        username = dlg.m_sUsername;
        password = dlg.m_sPassword;
        may_save = dlg.m_bSaveAuthentication;
        if (m_app)
            m_app->DoWaitCursor(0);
        return TRUE;
    }
    if (nResponse == IDABORT)
    {
        //the prompt dialog box could not be shown!
        ShowErrorMessage();
    }
    return FALSE;
}

void SVNPrompt::ShowErrorMessage()
{
    CFormatMessageWrapper errorDetails;
    MessageBox( FindParentWindow(m_hParentWnd), errorDetails, L"TortoiseSVN", MB_OK | MB_ICONINFORMATION );
}

svn_error_t* SVNPrompt::userprompt(svn_auth_cred_username_t **cred, void *baton, const char * realm, svn_boolean_t may_save, apr_pool_t *pool)
{
    SVNPrompt * svn = (SVNPrompt *)baton;
    svn_auth_cred_username_t *ret = (svn_auth_cred_username_t *)apr_pcalloc (pool, sizeof (*ret));
    CString username;
    CString temp;
    temp.LoadString(IDS_AUTH_USERNAME);
    if (!svn->m_bSuppressed && svn->Prompt(username, FALSE, temp, may_save))
    {
        ret->username = apr_pstrdup(pool, CUnicodeUtils::GetUTF8(username));
        ret->may_save = may_save;
        *cred = ret;
        Creds c;
        if (c.SetUsername(CStringA(username)))
            tsvn_creds[realm] = c;
    }
    else
    {
        if (svn->m_bSuppressed)
            svn->m_bPromptShown = true;
        *cred = NULL;
        if (!svn->m_bSuppressed)
            ::SendMessage(FindParentWindow(svn->m_hParentWnd), WM_SVNAUTHCANCELLED, 0, 0);
    }
    if (svn->m_app)
        svn->m_app->DoWaitCursor(0);
    return SVN_NO_ERROR;
}

svn_error_t* SVNPrompt::simpleprompt(svn_auth_cred_simple_t **cred, void *baton, const char * realm, const char *username, svn_boolean_t may_save, apr_pool_t *pool)
{
    SVNPrompt * svn = (SVNPrompt *)baton;
    svn_auth_cred_simple_t *ret = (svn_auth_cred_simple_t *)apr_pcalloc (pool, sizeof (*ret));
    CString UserName = CUnicodeUtils::GetUnicode(username);
    CString PassWord;
    CString Realm = CUnicodeUtils::GetUnicode(realm);
    if (!svn->m_bSuppressed && svn->SimplePrompt(UserName, PassWord, Realm, may_save))
    {
        ret->username = apr_pstrdup(pool, CUnicodeUtils::GetUTF8(UserName));
        ret->password = apr_pstrdup(pool, CUnicodeUtils::GetUTF8(PassWord));
        ret->may_save = may_save;
        *cred = ret;
        Creds c;
        if (c.SetUsername(ret->username) && c.SetPassword(ret->password))
            tsvn_creds[realm] = c;
    }
    else
    {
        if (svn->m_bSuppressed)
            svn->m_bPromptShown = true;
        *cred = NULL;
        if (!svn->m_bSuppressed)
            ::SendMessage(FindParentWindow(svn->m_hParentWnd), WM_SVNAUTHCANCELLED, 0, 0);
    }
    if (svn->m_app)
        svn->m_app->DoWaitCursor(0);
    return SVN_NO_ERROR;
}

svn_error_t* SVNPrompt::sslserverprompt(svn_auth_cred_ssl_server_trust_t **cred_p, void *baton, const char *realm, apr_uint32_t failures, const svn_auth_ssl_server_cert_info_t *cert_info, svn_boolean_t may_save, apr_pool_t *pool)
{
    SVNPrompt * svn = (SVNPrompt *)baton;

    BOOL prev = FALSE;

    CString msg;
    msg.Format(IDS_ERR_SSL_VALIDATE, (LPCTSTR)CUnicodeUtils::GetUnicode(realm));
    msg += L"\n";
    CString temp;
    if (failures & SVN_AUTH_SSL_UNKNOWNCA)
    {
        temp.FormatMessage(IDS_ERR_SSL_UNKNOWNCA, (LPCTSTR)CUnicodeUtils::GetUnicode(cert_info->fingerprint), (LPCTSTR)CUnicodeUtils::GetUnicode(cert_info->issuer_dname));
        msg += temp;
        prev = TRUE;
    }
    if (failures & SVN_AUTH_SSL_CNMISMATCH)
    {
        if (prev)
            msg += L"\n";
        temp.Format(IDS_ERR_SSL_CNMISMATCH, (LPCTSTR)CUnicodeUtils::GetUnicode(cert_info->hostname));
        msg += temp;
        prev = TRUE;
    }
    if (failures & SVN_AUTH_SSL_NOTYETVALID)
    {
        if (prev)
            msg += L"\n";
        temp.Format(IDS_ERR_SSL_NOTYETVALID, (LPCTSTR)CUnicodeUtils::GetUnicode(cert_info->valid_from));
        msg += temp;
        prev = TRUE;
    }
    if (failures & SVN_AUTH_SSL_EXPIRED)
    {
        if (prev)
            msg += L"\n";
        temp.Format(IDS_ERR_SSL_EXPIRED, (LPCTSTR)CUnicodeUtils::GetUnicode(cert_info->valid_until));
        msg += temp;
        prev = TRUE;
    }
    if (prev)
        msg += L"\n";
    temp.LoadString(IDS_SSL_ACCEPTQUESTION);
    msg += temp;

    *cred_p = NULL;
    if (!svn->m_bSuppressed)
    {
        if (may_save)
        {
            CTaskDialog taskdlg(msg,
                                CString(MAKEINTRESOURCE(IDS_SSL_ACCEPTQUESTION_TASK)),
                                L"TortoiseSVN",
                                0,
                                TDF_ENABLE_HYPERLINKS | TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW | TDF_SIZE_TO_CONTENT);
            taskdlg.AddCommandControl(IDCUSTOM1, CString(MAKEINTRESOURCE(IDS_SSL_ACCEPTALWAYS_TASK)));
            taskdlg.AddCommandControl(IDCUSTOM2, CString(MAKEINTRESOURCE(IDS_SSL_ACCEPTTEMP_TASK)));
            taskdlg.SetCommonButtons(TDCBF_CANCEL_BUTTON);
            taskdlg.SetMainIcon(TD_WARNING_ICON);
            UINT ret = (UINT)taskdlg.DoModal(svn->m_hParentWnd);
            if (ret == IDCUSTOM1)
            {
                *cred_p = (svn_auth_cred_ssl_server_trust_t*)apr_pcalloc (pool, sizeof (**cred_p));
                (*cred_p)->may_save = TRUE;
                (*cred_p)->accepted_failures = failures;
            }
            else if (ret == IDCUSTOM2)
            {
                *cred_p = (svn_auth_cred_ssl_server_trust_t*)apr_pcalloc (pool, sizeof (**cred_p));
                (*cred_p)->may_save = FALSE;
                (*cred_p)->accepted_failures = failures;
                svn->m_bPromptShown = true;
            }
        }
        else
        {
            CTaskDialog taskdlg(msg,
                                CString(MAKEINTRESOURCE(IDS_SSL_ACCEPTQUESTION_TASK)),
                                L"TortoiseSVN",
                                0,
                                TDF_ENABLE_HYPERLINKS | TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW | TDF_SIZE_TO_CONTENT);
            taskdlg.AddCommandControl(IDCUSTOM2, CString(MAKEINTRESOURCE(IDS_SSL_ACCEPTTEMP_TASK)));
            taskdlg.AddCommandControl(IDCUSTOM3, CString(MAKEINTRESOURCE(IDS_SSL_REJECT_TASK)));
            taskdlg.SetCommonButtons(TDCBF_CANCEL_BUTTON);
            taskdlg.SetDefaultCommandControl(IDCUSTOM3);
            taskdlg.SetMainIcon(TD_WARNING_ICON);
            bool bAccept = (taskdlg.DoModal(svn->m_hParentWnd) == IDCUSTOM2);
            if (bAccept)
            {
                *cred_p = (svn_auth_cred_ssl_server_trust_t*)apr_pcalloc (pool, sizeof (**cred_p));
                (*cred_p)->may_save = FALSE;
                (*cred_p)->accepted_failures = failures;
                svn->m_bPromptShown = true;
            }
        }
    }
    else
    {
        // if the UI is suppressed, accept invalid certs but do not save that choice
        *cred_p = (svn_auth_cred_ssl_server_trust_t*)apr_pcalloc(pool, sizeof(**cred_p));
        (*cred_p)->may_save = FALSE;
        (*cred_p)->accepted_failures = failures;
        svn->m_bPromptShown = true;
    }

    if (svn->m_app)
        svn->m_app->DoWaitCursor(0);
    return SVN_NO_ERROR;
}

svn_error_t* SVNPrompt::sslclientprompt(svn_auth_cred_ssl_client_cert_t **cred, void *baton, const char * realm, svn_boolean_t /*may_save*/, apr_pool_t *pool)
{
    SVNPrompt * svn = (SVNPrompt *)baton;
    const char *cert_file = NULL;

    svn->m_bPromptShown = true;
    CString filename;

    BOOL bOpenRet = FALSE;
    bool may_save = false;

    if (!svn->m_bSuppressed)
    {
        HRESULT hr;
        // Create a new common open file dialog
        CComPtr<IFileOpenDialog> pfd = NULL;
        hr = pfd.CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER);
        if (SUCCEEDED(hr))
        {
            // Set the dialog options
            DWORD dwOptions;
            if (SUCCEEDED(hr = pfd->GetOptions(&dwOptions)))
            {
                hr = pfd->SetOptions(dwOptions | FOS_FILEMUSTEXIST | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
            }

            // Set a title
            if (SUCCEEDED(hr))
            {
                CString temp;
                temp.LoadString(IDS_SSL_CLIENTCERTIFICATEFILENAME);
                CStringUtils::RemoveAccelerators(temp);
                pfd->SetTitle(temp);
            }
            CSelectFileFilter fileFilter(IDS_CERTIFICATESFILEFILTER);

            hr = pfd->SetFileTypes(fileFilter.GetCount(), fileFilter);

            CComPtr<IFileDialogCustomize> pfdCustomize;
            hr = pfd.QueryInterface(&pfdCustomize);
            if (SUCCEEDED(hr))
            {
                pfdCustomize->AddCheckButton(101, CString(MAKEINTRESOURCE(IDS_SSL_SAVE_CERTPATH)), FALSE);
            }

            // Show the save file dialog
            if (SUCCEEDED(hr) && SUCCEEDED(hr = pfd->Show(GetExplorerHWND())))
            {
                CComPtr<IFileDialogCustomize> pfdCustomizeRet;
                hr = pfd.QueryInterface(&pfdCustomizeRet);
                if (SUCCEEDED(hr))
                {
                    BOOL bChecked = FALSE;
                    pfdCustomizeRet->GetCheckButtonState(101, &bChecked);
                    may_save = (bChecked != 0);
                }

                // Get the selection from the user
                CComPtr<IShellItem> psiResult = NULL;
                hr = pfd->GetResult(&psiResult);
                if (SUCCEEDED(hr))
                {
                    PWSTR pszPath = NULL;
                    hr = psiResult->GetDisplayName(SIGDN_FILESYSPATH, &pszPath);
                    if (SUCCEEDED(hr))
                    {
                        filename = CString(pszPath);
                        bOpenRet = TRUE;
                    }
                }
            }
        }
        else
        {
            OPENFILENAME ofn = { 0 };             // common dialog box structure
            TCHAR szFile[MAX_PATH] = { 0 };       // buffer for file name
            // Initialize OPENFILENAME
            ofn.lStructSize = sizeof(OPENFILENAME);
            ofn.hwndOwner = svn->m_hParentWnd;
            ofn.lpstrFile = szFile;
            ofn.nMaxFile = _countof(szFile);
            CSelectFileFilter fileFilter(IDS_CERTIFICATESFILEFILTER);
            ofn.lpstrFilter = fileFilter;
            ofn.nFilterIndex = 1;
            ofn.lpstrFileTitle = NULL;
            ofn.nMaxFileTitle = 0;
            ofn.lpstrInitialDir = NULL;
            CString temp;
            temp.LoadString(IDS_SSL_CLIENTCERTIFICATEFILENAME);
            CStringUtils::RemoveAccelerators(temp);
            ofn.lpstrTitle = temp;
            ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_ENABLEHOOK | OFN_ENABLESIZING | OFN_EXPLORER;
            ofn.lpfnHook = SVNPrompt::OFNHookProc;

            // Display the Open dialog box.
            svn->m_server.Empty();
            bOpenRet = GetOpenFileName(&ofn);
            if (bOpenRet)
            {
                filename = CString(ofn.lpstrFile);
                may_save = ((ofn.Flags & OFN_READONLY) != 0);
            }
        }
    }
    if (!svn->m_bSuppressed && (bOpenRet==TRUE))
    {
        cert_file = apr_pstrdup(pool, CUnicodeUtils::GetUTF8(filename));
        /* Build and return the credentials. */
        *cred = (svn_auth_cred_ssl_client_cert_t*)apr_pcalloc (pool, sizeof (**cred));
        (*cred)->cert_file = cert_file;
        (*cred)->may_save = may_save;

        // the svn library doesn't have a save_credentials() function for cert files
        // (yet?). It would get implemented in subversion\libsvn_subr\ssl_client_cert_providers.c
        //
        // We do the saving here ourselves (until subversion implements its own saving)
        if (may_save)
        {
            CString regpath = L"Software\\tigris.org\\Subversion\\Servers\\";
            CString groups = regpath;
            groups += L"groups\\";
            CString server = CString(realm);
            int f1 = server.Find('<')+9;
            int len = server.Find(':', 10)-f1;
            server = server.Mid(f1, len);
            svn->m_server = server;
            groups += server;
            CRegString server_groups(groups);
            server_groups = server;
            regpath += server;
            regpath += L"\\ssl-client-cert-file";
            CRegString client_cert_filepath_reg(regpath);
            client_cert_filepath_reg = filename;
        }
    }
    else
    {
        if (svn->m_bSuppressed)
            svn->m_bPromptShown = true;
        *cred = NULL;
    }
    if (svn->m_app)
        svn->m_app->DoWaitCursor(0);
    return SVN_NO_ERROR;
}

UINT_PTR CALLBACK SVNPrompt::OFNHookProc(HWND hdlg, UINT uiMsg, WPARAM /*wParam*/, LPARAM lParam)
{
    CString temp;
    switch (uiMsg)
    {

    case WM_NOTIFY:
        // look for the CDN_INITDONE notification telling you Windows
        // has has finished initializing the dialog box
        LPOFNOTIFY lpofn = (LPOFNOTIFY)lParam;
        if (lpofn->hdr.code == CDN_INITDONE)
        {
            temp.LoadString(IDS_SSL_SAVE_CERTPATH);
            SendMessage(::GetParent(hdlg), CDM_SETCONTROLTEXT, chx1, (LPARAM)(LPCTSTR)temp);
            return TRUE;
        }
    }
    return FALSE;
}

svn_error_t* SVNPrompt::sslpwprompt(svn_auth_cred_ssl_client_cert_pw_t **cred, void *baton, const char * /*realm*/, svn_boolean_t may_save, apr_pool_t *pool)
{
    SVNPrompt* svn = (SVNPrompt *)baton;
    svn_auth_cred_ssl_client_cert_pw_t *ret = (svn_auth_cred_ssl_client_cert_pw_t *)apr_pcalloc (pool, sizeof (*ret));
    CString password;
    CString temp;
    temp.LoadString(IDS_AUTH_PASSWORD);
    if (!svn->m_bSuppressed && svn->Prompt(password, TRUE, temp, may_save))
    {
        ret->password = apr_pstrdup(pool, CUnicodeUtils::GetUTF8(password));
        ret->may_save = may_save;
        *cred = ret;
    }
    else
    {
        if (svn->m_bSuppressed)
            svn->m_bPromptShown = true;
        *cred = NULL;
    }
    if (svn->m_app)
        svn->m_app->DoWaitCursor(0);
    return SVN_NO_ERROR;
}

svn_error_t* SVNPrompt::svn_auth_plaintext_prompt(svn_boolean_t *may_save_plaintext, const char * /*realmstring*/, void * /*baton*/, apr_pool_t * /*pool*/)
{
    // we allow saving plaintext passwords without asking the user. The reason is simple:
    // TSVN requires at least Vista, which means the password is always stored encrypted because
    // the corresponding APIs are available.
    // If for some unknown reason it wouldn't be possible to save the passwords encrypted,
    // most users wouldn't know what to do anyway so asking them would just confuse them.
    *may_save_plaintext = true;
    return SVN_NO_ERROR;
}

svn_error_t* SVNPrompt::svn_auth_plaintext_passphrase_prompt(svn_boolean_t *may_save_plaintext, const char * /*realmstring*/, void * /*baton*/, apr_pool_t * /*pool*/)
{
    // we allow saving plaintext passwords without asking the user. The reason is simple:
    // TSVN requires at least Vista, which means the password is always stored encrypted because
    // the corresponding APIs are available.
    // If for some unknown reason it wouldn't be possible to save the passwords encrypted,
    // most users wouldn't know what to do anyway so asking them would just confuse them.
    *may_save_plaintext = true;
    return SVN_NO_ERROR;
}

