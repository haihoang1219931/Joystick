#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <stdio.h>
#define STRICT
#define DIRECTINPUT_VERSION 0x0800
#define _CRT_SECURE_NO_DEPRECATE
#ifndef _WIN32_DCOM
#define _WIN32_DCOM
#endif

#include <windows.h>
#include <stdio.h>
#include <tchar.h>
#include <commctrl.h>
#include <basetsd.h>

#pragma warning(push)
#pragma warning(disable:6000 28251)
#include <dinput.h>
#pragma warning(pop)

#include <dinputd.h>
#include <assert.h>
#include <oleauto.h>
#include <shellapi.h>
//#include "resource.h"
BOOL CALLBACK  EnumObjectsCallback( const DIDEVICEOBJECTINSTANCE* pdidoi, VOID* pContext );
BOOL CALLBACK  EnumJoysticksCallback( const DIDEVICEINSTANCE* pdidInstance, VOID* pContext );
HRESULT InitDirectInput();
VOID FreeDirectInput();
HRESULT UpdateInputState();
// Stuff to filter out XInput devices
#include <wbemidl.h>
HRESULT SetupForIsXInputDevice();
bool IsXInputDevice( const GUID* pGuidProductFromDirectInput );
void CleanupForIsXInputDevice();
HRESULT UpdateInputState();
struct XINPUT_DEVICE_NODE
{
    DWORD dwVidPid;
    XINPUT_DEVICE_NODE* pNext;
};

struct DI_ENUM_CONTEXT
{
    DIJOYCONFIG* pPreferredJoyCfg;
    bool bPreferredJoyCfgValid;
};

bool                    g_bFilterOutXinputDevices = true;
XINPUT_DEVICE_NODE*     g_pXInputDeviceList = nullptr;

//-----------------------------------------------------------------------------
// Defines, constants, and global variables
//-----------------------------------------------------------------------------
#define SAFE_DELETE(p)  { if(p) { delete (p);     (p)=nullptr; } }
#define SAFE_RELEASE(p) { if(p) { (p)->Release(); (p)=nullptr; } }

LPDIRECTINPUT8          g_pDI = nullptr;
LPDIRECTINPUTDEVICE8    g_pJoystick = nullptr;

int main(int argc, char *argv[])
{
//    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
//    QGuiApplication app(argc, argv);

//    QQmlApplicationEngine engine;
//    engine.load(QUrl(QLatin1String("qrc:/main.qml")));
//    if (engine.rootObjects().isEmpty())
//        return -1;

//    return app.exec();
    printf("Hello World!\r\n");
    InitDirectInput();
    while(1){
        UpdateInputState();
        Sleep(30);
    }
    return 0;
}
BOOL CALLBACK EnumJoysticksCallback( const DIDEVICEINSTANCE* pdidInstance,
                                     VOID* pContext )
{
    auto pEnumContext = reinterpret_cast<DI_ENUM_CONTEXT*>( pContext );
    HRESULT hr;

    if( g_bFilterOutXinputDevices && IsXInputDevice( &pdidInstance->guidProduct ) )
        return DIENUM_CONTINUE;

    // Skip anything other than the perferred joystick device as defined by the control panel.
    // Instead you could store all the enumerated joysticks and let the user pick.
    if( pEnumContext->bPreferredJoyCfgValid &&
        !IsEqualGUID( pdidInstance->guidInstance, pEnumContext->pPreferredJoyCfg->guidInstance ) )
        return DIENUM_CONTINUE;

    // Obtain an interface to the enumerated joystick.
    hr = g_pDI->CreateDevice( pdidInstance->guidInstance, &g_pJoystick, nullptr );

    // If it failed, then we can't use this joystick. (Maybe the user unplugged
    // it while we were in the middle of enumerating it.)
    if( FAILED( hr ) )
        return DIENUM_CONTINUE;

    // Stop enumeration. Note: we're just taking the first joystick we get. You
    // could store all the enumerated joysticks and let the user pick.
    return DIENUM_STOP;
}
HRESULT SetupForIsXInputDevice()
{
    IWbemServices* pIWbemServices = nullptr;
    IEnumWbemClassObject* pEnumDevices = nullptr;
    IWbemLocator* pIWbemLocator = nullptr;
    IWbemClassObject* pDevices[20] = {0};
    BSTR bstrDeviceID = nullptr;
    BSTR bstrClassName = nullptr;
    BSTR bstrNamespace = nullptr;
    DWORD uReturned = 0;
    bool bCleanupCOM = false;
    UINT iDevice = 0;
    VARIANT var;
    HRESULT hr;

    // CoInit if needed
    hr = CoInitialize( nullptr );
    bCleanupCOM = SUCCEEDED( hr );

    // Create WMI
    hr = CoCreateInstance( __uuidof( WbemLocator ),
                           nullptr,
                           CLSCTX_INPROC_SERVER,
                           __uuidof( IWbemLocator ),
                           ( LPVOID* )&pIWbemLocator );
    if( FAILED( hr ) || pIWbemLocator == nullptr )
        goto LCleanup;

    // Create BSTRs for WMI
    bstrNamespace = SysAllocString( L"\\\\.\\root\\cimv2" ); if( bstrNamespace == nullptr ) goto LCleanup;
    bstrDeviceID = SysAllocString( L"DeviceID" );           if( bstrDeviceID == nullptr )  goto LCleanup;
    bstrClassName = SysAllocString( L"Win32_PNPEntity" );    if( bstrClassName == nullptr ) goto LCleanup;

    // Connect to WMI
    hr = pIWbemLocator->ConnectServer( bstrNamespace, nullptr, nullptr, 0L,
                                       0L, nullptr, nullptr, &pIWbemServices );
    if( FAILED( hr ) || pIWbemServices == nullptr )
        goto LCleanup;

    // Switch security level to IMPERSONATE
    (void)CoSetProxyBlanket( pIWbemServices, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                             RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, 0 );

    // Get list of Win32_PNPEntity devices
    hr = pIWbemServices->CreateInstanceEnum( bstrClassName, 0, nullptr, &pEnumDevices );
    if( FAILED( hr ) || pEnumDevices == nullptr )
        goto LCleanup;

    // Loop over all devices
    for(; ; )
    {
        // Get 20 at a time
        hr = pEnumDevices->Next( 10000, 20, pDevices, &uReturned );
        if( FAILED( hr ) )
            goto LCleanup;
        if( uReturned == 0 )
            break;

        for( iDevice = 0; iDevice < uReturned; iDevice++ )
        {
            if ( !pDevices[iDevice] )
               continue;

            // For each device, get its device ID
            hr = pDevices[iDevice]->Get( bstrDeviceID, 0L, &var, nullptr, nullptr );
            if( SUCCEEDED( hr ) && var.vt == VT_BSTR && var.bstrVal != nullptr )
            {
                // Check if the device ID contains "IG_".  If it does, then it�s an XInput device
                // Unfortunately this information can not be found by just using DirectInput
                if( wcsstr( var.bstrVal, L"IG_" ) )
                {
                    // If it does, then get the VID/PID from var.bstrVal
                    DWORD dwPid = 0, dwVid = 0;
                    WCHAR* strVid = wcsstr( var.bstrVal, L"VID_" );
                    if( strVid && swscanf( strVid, L"VID_%4X", &dwVid ) != 1 )
                        dwVid = 0;
                    WCHAR* strPid = wcsstr( var.bstrVal, L"PID_" );
                    if( strPid && swscanf( strPid, L"PID_%4X", &dwPid ) != 1 )
                        dwPid = 0;

                    DWORD dwVidPid = MAKELONG( dwVid, dwPid );

                    // Add the VID/PID to a linked list
                    XINPUT_DEVICE_NODE* pNewNode = new XINPUT_DEVICE_NODE;
                    if( pNewNode )
                    {
                        pNewNode->dwVidPid = dwVidPid;
                        pNewNode->pNext = g_pXInputDeviceList;
                        g_pXInputDeviceList = pNewNode;
                    }
                }
            }
            SAFE_RELEASE( pDevices[iDevice] );
        }
    }

LCleanup:
    if( bstrNamespace )
        SysFreeString( bstrNamespace );
    if( bstrDeviceID )
        SysFreeString( bstrDeviceID );
    if( bstrClassName )
        SysFreeString( bstrClassName );
    for( iDevice = 0; iDevice < 20; iDevice++ )
    SAFE_RELEASE( pDevices[iDevice] );
    SAFE_RELEASE( pEnumDevices );
    SAFE_RELEASE( pIWbemLocator );
    SAFE_RELEASE( pIWbemServices );
    printf("SetupForIsXInputDevice\r\n");
    return hr;
}


//-----------------------------------------------------------------------------
// Returns true if the DirectInput device is also an XInput device.
// Call SetupForIsXInputDevice() before, and CleanupForIsXInputDevice() after
//-----------------------------------------------------------------------------
bool IsXInputDevice( const GUID* pGuidProductFromDirectInput )
{
    // Check each xinput device to see if this device's vid/pid matches
    XINPUT_DEVICE_NODE* pNode = g_pXInputDeviceList;
    while( pNode )
    {
        if( pNode->dwVidPid == pGuidProductFromDirectInput->Data1 )
            return true;
        pNode = pNode->pNext;
    }

    return false;
}


//-----------------------------------------------------------------------------
// Cleanup needed for IsXInputDevice()
//-----------------------------------------------------------------------------
void CleanupForIsXInputDevice()
{
    // Cleanup linked list
    XINPUT_DEVICE_NODE* pNode = g_pXInputDeviceList;
    while( pNode )
    {
        XINPUT_DEVICE_NODE* pDelete = pNode;
        pNode = pNode->pNext;
        SAFE_DELETE( pDelete );
    }
}

HRESULT InitDirectInput()
{
    HRESULT hr;

    // Register with the DirectInput subsystem and get a pointer
    // to a IDirectInput interface we can use.
    // Create a DInput object
    if( FAILED( hr = DirectInput8Create( GetModuleHandle( nullptr ), DIRECTINPUT_VERSION,
                                         IID_IDirectInput8, ( VOID** )&g_pDI, nullptr ) ) )
        return hr;


    if( g_bFilterOutXinputDevices )
        SetupForIsXInputDevice();

    DIJOYCONFIG PreferredJoyCfg = {0};
    DI_ENUM_CONTEXT enumContext;
    enumContext.pPreferredJoyCfg = &PreferredJoyCfg;
    enumContext.bPreferredJoyCfgValid = false;

    IDirectInputJoyConfig8* pJoyConfig = nullptr;
    if( FAILED( hr = g_pDI->QueryInterface( IID_IDirectInputJoyConfig8, ( void** )&pJoyConfig ) ) )
        return hr;

    PreferredJoyCfg.dwSize = sizeof( PreferredJoyCfg );
    if( SUCCEEDED( pJoyConfig->GetConfig( 0, &PreferredJoyCfg, DIJC_GUIDINSTANCE ) ) ) // This function is expected to fail if no joystick is attached
        enumContext.bPreferredJoyCfgValid = true;
    SAFE_RELEASE( pJoyConfig );

    // Look for a simple joystick we can use for this sample program.
    if( FAILED( hr = g_pDI->EnumDevices( DI8DEVCLASS_GAMECTRL,
                                         EnumJoysticksCallback,
                                         &enumContext, DIEDFL_ATTACHEDONLY ) ) )
        return hr;

    if( g_bFilterOutXinputDevices )
        CleanupForIsXInputDevice();

    // Make sure we got a joystick
    if( !g_pJoystick )
    {
        printf("Joystick not found\r\n");
        return S_OK;
    }
    // Set the data format to "simple joystick" - a predefined data format
    //
    // A data format specifies which controls on a device we are interested in,
    // and how they should be reported. This tells DInput that we will be
    // passing a DIJOYSTATE2 structure to IDirectInputDevice::GetDeviceState().
    if( FAILED( hr = g_pJoystick->SetDataFormat( &c_dfDIJoystick2 ) ) )
        return hr;
    return S_OK;
}
//-----------------------------------------------------------------------------
// Name: UpdateInputState()
// Desc: Get the input device's state and display it.
//-----------------------------------------------------------------------------
HRESULT UpdateInputState()
{

    HRESULT hr;
    TCHAR strText[512] = {0}; // Device state text
    DIJOYSTATE2 js;           // DInput joystick state

    if( !g_pJoystick ){
        return S_OK;
    }

    // Poll the device to read the current state
    hr = g_pJoystick->Poll();
    printf("UpdateInputState\r\n");
    if( FAILED( hr ) )
    {
        // DInput is telling us that the input stream has been
        // interrupted. We aren't tracking any state between polls, so
        // we don't have any special reset that needs to be done. We
        // just re-acquire and try again.
        hr = g_pJoystick->Acquire();
        while( hr == DIERR_INPUTLOST )
            hr = g_pJoystick->Acquire();

        // hr may be DIERR_OTHERAPPHASPRIO or other errors.  This
        // may occur when the app is minimized or in the process of
        // switching, so just try again later
        printf("g_pJoystick->Poll ERROR\r\n");
        return S_OK;
    }else{
        printf("g_pJoystick->Poll OK\r\n");
    }

    // Get the input's device state
    if( FAILED( hr = g_pJoystick->GetDeviceState( sizeof( DIJOYSTATE2 ), &js ) ) )
        return hr; // The device should have been acquired during the Poll()

    // Display joystick state to dialog
    printf("Axis: (%ld,%ld)\r\n",js.lX,js.lY);
//    // Axes
//    _stprintf_s( strText, 512, TEXT( "%ld" ), js.lX );
//    SetWindowText( GetDlgItem( hDlg, IDC_X_AXIS ), strText );
//    _stprintf_s( strText, 512, TEXT( "%ld" ), js.lY );
//    SetWindowText( GetDlgItem( hDlg, IDC_Y_AXIS ), strText );
//    _stprintf_s( strText, 512, TEXT( "%ld" ), js.lZ );
//    SetWindowText( GetDlgItem( hDlg, IDC_Z_AXIS ), strText );
//    _stprintf_s( strText, 512, TEXT( "%ld" ), js.lRx );
//    SetWindowText( GetDlgItem( hDlg, IDC_X_ROT ), strText );
//    _stprintf_s( strText, 512, TEXT( "%ld" ), js.lRy );
//    SetWindowText( GetDlgItem( hDlg, IDC_Y_ROT ), strText );
//    _stprintf_s( strText, 512, TEXT( "%ld" ), js.lRz );
//    SetWindowText( GetDlgItem( hDlg, IDC_Z_ROT ), strText );

//    // Slider controls
//    _stprintf_s( strText, 512, TEXT( "%ld" ), js.rglSlider[0] );
//    SetWindowText( GetDlgItem( hDlg, IDC_SLIDER0 ), strText );
//    _stprintf_s( strText, 512, TEXT( "%ld" ), js.rglSlider[1] );
//    SetWindowText( GetDlgItem( hDlg, IDC_SLIDER1 ), strText );

//    // Points of view
//    _stprintf_s( strText, 512, TEXT( "%lu" ), js.rgdwPOV[0] );
//    SetWindowText( GetDlgItem( hDlg, IDC_POV0 ), strText );
//    _stprintf_s( strText, 512, TEXT( "%lu" ), js.rgdwPOV[1] );
//    SetWindowText( GetDlgItem( hDlg, IDC_POV1 ), strText );
//    _stprintf_s( strText, 512, TEXT( "%lu" ), js.rgdwPOV[2] );
//    SetWindowText( GetDlgItem( hDlg, IDC_POV2 ), strText );
//    _stprintf_s( strText, 512, TEXT( "%lu" ), js.rgdwPOV[3] );
//    SetWindowText( GetDlgItem( hDlg, IDC_POV3 ), strText );


//    // Fill up text with which buttons are pressed
//    _tcscpy_s( strText, 512, TEXT( "" ) );
//    for( int i = 0; i < 128; i++ )
//    {
//        if( js.rgbButtons[i] & 0x80 )
//        {
//            TCHAR sz[128];
//            _stprintf_s( sz, 128, TEXT( "%02d " ), i );
//            _tcscat_s( strText, 512, sz );
//        }
//    }

//    SetWindowText( GetDlgItem( hDlg, IDC_BUTTONS ), strText );

    return S_OK;
}


