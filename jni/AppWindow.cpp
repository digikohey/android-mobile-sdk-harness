#include <stdio.h>

#include "AppWindow.hpp"
#include "AndroidWebRequestService.hpp"
#include "AndroidTaskQueue.h"
#include "LatLongAltitude.h"
#include "EegeoWorld.h"
#include "RenderContext.h"
#include "Camera.h"
#include "CameraModel.h"
#include "NewGlobeCamera.h"
#include "GlobalLighting.h"
#include "GlobalFogging.h"
#include "DefaultMaterialFactory.h"
#include "AppInterface.h"
#include "Blitter.h"
#include "EffectHandler.h"
#include "VehicleModelLoader.h"
#include "VehicleModelRepository.h"
#include "SearchServiceCredentials.h"

using namespace Eegeo::Android;
using namespace Eegeo::Android::Input;

#define API_KEY "OBTAIN API_KEY FROM https://appstore.eegeo.com AND INSERT IT HERE"

AppWindow::AppWindow(struct android_app* pState)
: pState(pState)
, pAppOnMap(NULL)
, pInputProcessor(NULL)
, pWorld(NULL)
, active(false)
, firstTime(true)
, m_androidInputBoxFactory(pState)
, m_androidKeyboardInputFactory(pState, pInputHandler)
, m_androidAlertBoxFactory(pState)
, m_androidNativeUIFactories(m_androidAlertBoxFactory, m_androidInputBoxFactory, m_androidKeyboardInputFactory)
, lastGlobeCameraLatLong(0,0,0)
, m_terrainHeightRepository()
, m_terrainHeightProvider(&m_terrainHeightRepository)
{
	//Eegeo_TTY("CONSTRUCTING AppWindow");
}

void AppWindow::Run()
{
	//Eegeo_TTY("STARTING RUN");

    while (1)
    {
    	// Read all pending events.
        int ident;
        int events;
        struct android_poll_source* source;

        // If not animating, we will block forever waiting for events.
        // If animating, we loop until all events are read, then continue
        // to draw the next frame of animation.
        while ((ident=ALooper_pollAll(0, NULL, &events, (void**)&source)) >= 0) {

            // Process this event.
            if (source != NULL) {
                source->process(pState, source);
            }

            if (pState->destroyRequested != 0) {
            	exit(0);
            }
        }

        if(active) {
        	UpdateWorld();
        }
        else {
        	usleep(100000);
        }
    }
}

void AppWindow::UpdateWorld()
{
	if(pWorld!= NULL)
	{
		//Eegeo_TTY("UPDATING WORLD");
	    Eegeo_GL(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT));

		float fps = 1.0f/30.0f;
		pAndroidWebRequestService->Update();
		pAppOnMap->Update(fps);
		pAppOnMap->Draw(fps);
		currentWeatherModel.SetWeatherType(pAppOnMap->World().GetWeatherController().GetWeatherType());

		Eegeo_GL(glFinish());
		Eegeo_GL(eglSwapBuffers(display, surface));
	}
}


int32_t AppWindow::HandleInput(AInputEvent* event)
{
	if(pWorld==NULL)
	{
		return 0;
	}

	return pInputProcessor->HandleInput(event);
}

void AppWindow::HandleCommand(int32_t cmd)
{
    switch (cmd)
    {
        case APP_CMD_INIT_WINDOW:
        	//Eegeo_TTY("APP_CMD_INIT_WINDOW");
        	InitDisplay();
        	active = true;
            break;
        case APP_CMD_TERM_WINDOW:
        	//Eegeo_TTY("APP_CMD_TERM_WINDOW");
        	TerminateDisplay();
        	active = false;
            break;
    }
}

void AppWindow::InitDisplay()
{
    // initialize OpenGL ES and EGL
    const EGLint attribs[] = {
    		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_BLUE_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_RED_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            EGL_DEPTH_SIZE, 16,
            EGL_STENCIL_SIZE, 8,
            EGL_NONE
    };

    EGLint w, h, dummy, format;
    EGLint numConfigs;
    EGLConfig config;
    EGLSurface surface;
    EGLContext context;

    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);

    eglInitialize(display, 0, 0);
    eglChooseConfig(display, attribs, &config, 1, &numConfigs);
    eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &format);

    ANativeWindow_setBuffersGeometry(pState->window, 0, 0, format);

    static const EGLint contextAttribs[] = {
              EGL_CONTEXT_CLIENT_VERSION,        2,
              EGL_NONE
           };

    surface = eglCreateWindowSurface(display, config, pState->window, NULL);
    context = eglCreateContext(display, config, NULL, contextAttribs);

    if (eglMakeCurrent(display, surface, surface, context) == EGL_FALSE) {
        Eegeo_TTY("Unable to eglMakeCurrent");

        return;
    }

    //Eegeo_TTY("printing extensions\n");
    //char * extensionsString =  (char *) glGetString(GL_EXTENSIONS);
    //Eegeo_TTY("%s\n",extensionsString);

    Eegeo_GL(eglQuerySurface(display, surface, EGL_WIDTH, &w));
    Eegeo_GL(eglQuerySurface(display, surface, EGL_HEIGHT, &h));

    this->display = display;
    this->context = context;
    this->surface = surface;

#ifdef EEGEO_DROID_EMULATOR
    this->shareSurface = EGL_NO_SURFACE;
    this->resourceBuildShareContext = EGL_NO_CONTEXT;
#else
    resourceBuildShareContext = eglCreateContext(display, config, context, contextAttribs);

    const EGLint sharedSurfaceAttributes[] = {
      		  EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
              EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
              EGL_BLUE_SIZE, 8,
              EGL_GREEN_SIZE, 8,
              EGL_RED_SIZE, 8,
              EGL_ALPHA_SIZE, 8,
              EGL_DEPTH_SIZE, 16,
              EGL_STENCIL_SIZE, 8,
              EGL_NONE,

      };

    EGLint pbufferAttribs[] =
        {
            EGL_WIDTH, 1,
            EGL_HEIGHT, 1,
            EGL_NONE
        };
    EGLConfig sharedSurfaceConfig;
    eglChooseConfig(display, sharedSurfaceAttributes, &sharedSurfaceConfig, 1, &numConfigs);
    this->shareSurface = eglCreatePbufferSurface(display, sharedSurfaceConfig, pbufferAttribs);
#endif
    this->width = w;
    this->height = h;

    // Initialize GL state.
    Eegeo_GL(glClearDepthf(1.0f));
	Eegeo_GL(glClearColor(0.0f, 0.0f, 0.0f, 1.0f));

	// Set up default Depth test.
	Eegeo_GL(glEnable(GL_DEPTH_TEST));
	Eegeo_GL(glDepthMask(GL_TRUE));
	Eegeo_GL(glDepthFunc(GL_LEQUAL))

	// Set up default culling.
	Eegeo_GL(glEnable(GL_CULL_FACE));
	Eegeo_GL(glFrontFace(GL_CW));
	Eegeo_GL(glCullFace(GL_BACK));

	// Turn off the stencil test.
	Eegeo_GL(glDisable(GL_STENCIL_TEST));
	Eegeo_GL(glStencilFunc(GL_NEVER, 0, 0xFFFFFFFF));
	Eegeo_GL(glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP));

	// Set the default blend mode and colour mask.
	Eegeo_GL(glDisable(GL_BLEND));
	Eegeo_GL(glColorMask(true, true, true, true));

	//when window and gl context are ready, init the world
	InitWorld();
}


void AppWindow::TerminateDisplay()
{
	delete pAppOnMap;
	pHttpCache->FlushInMemoryCacheRepresentation();

    delete pTaskQueue;

    delete m_pGlobeCameraInterestPointProvider;

	lastGlobeCameraDistanceToInterest = pGlobeCamera->GetDistanceToInterest();
	lastGlobeCameraHeading = pGlobeCamera->GetHeading();
	lastGlobeCameraLatLong = Eegeo::Space::LatLongAltitude::FromECEF(pGlobeCamera->GetInterestPointECEF());

    delete pWorld;
    pWorld = NULL;

    delete pAndroidUrlEncoder;
    delete pAndroidLocationService;
    delete pRenderContext;
    delete pCamera;
    delete pCameraModel ;
	delete pGlobeCamera;
    delete pLighting;
    delete pFileIO;
    delete pHttpCache;
    delete pTextureLoader;
    Eegeo::EffectHandler::Reset();
    Eegeo::EffectHandler::Shutdown();
    pBlitter->Shutdown();
    delete pBlitter;
    delete pMaterialFactory;
    delete pAndroidWebRequestService;
    delete pAndroidWebLoadRequestFactory;
    delete pVehicleModelRepository;
    delete pVehicleModelLoader;

    if (this->display != EGL_NO_DISPLAY)
    {
    	Eegeo_GL(eglMakeCurrent(this->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT));

    	if (this->surface != EGL_NO_SURFACE)
		{
			Eegeo_GL(eglDestroySurface(this->display, this->surface));
		}

    	if (this->shareSurface != EGL_NO_SURFACE)
		{
    		Eegeo_GL(eglDestroySurface(this->display, this->shareSurface));
		}

    	if (this->context != EGL_NO_CONTEXT)
        {
        	Eegeo_GL(eglDestroyContext(this->display, this->context));
        }

        if(this->resourceBuildShareContext != EGL_NO_CONTEXT)
        {
        	Eegeo_GL(eglDestroyContext(this->display, this->resourceBuildShareContext));
        }

        Eegeo_GL(eglTerminate(this->display));
    }

    this->display = EGL_NO_DISPLAY;
    this->context = EGL_NO_CONTEXT;
    this->resourceBuildShareContext = EGL_NO_CONTEXT;
    this->surface = EGL_NO_SURFACE;
    this->shareSurface = EGL_NO_SURFACE;
}

void AppWindow::InitWorld()
{
	pAndroidUrlEncoder = new AndroidUrlEncoder(pState);
	pAndroidLocationService = new AndroidLocationService(pState);

	pRenderContext = new Eegeo::Rendering::RenderContext();
	pRenderContext->SetScreenDimensions(width, height, 1.0f);

    pCamera = new Eegeo::RenderCamera;
    pCamera->SetViewport(0.f, 0.f, width, height);

    pCameraModel = new Eegeo::Camera::CameraModel(pCamera);
    pGlobeCamera = new Eegeo::Camera::NewGlobeCamera(pCameraModel, pCamera, m_terrainHeightProvider);

    m_pGlobeCameraInterestPointProvider = new Eegeo::Location::GlobeCameraInterestPointProvider(*pGlobeCamera);

    pLighting = new Eegeo::Lighting::GlobalLighting();
	pFogging = new Eegeo::Lighting::GlobalFogging();

	pFileIO = new AndroidFileIO(pState);
	pHttpCache = new AndroidHttpCache(pFileIO, "http://d2xvsc8j92rfya.cloudfront.net/");
	pTextureLoader = new AndroidTextureFileLoader(pFileIO, pRenderContext->GetGLState());

	Eegeo::EffectHandler::Initialise();
	pBlitter = new Eegeo::Blitter(1024 * 128, 1024 * 64, 1024 * 32, *pRenderContext);
	pBlitter->Initialise();

	pMaterialFactory = new Eegeo::Rendering::DefaultMaterialFactory;
	pMaterialFactory->Initialise(&currentWeatherModel, pRenderContext, pLighting, pFogging, pBlitter, pFileIO, pTextureLoader);

	pTaskQueue = new AndroidTaskQueue(10, resourceBuildShareContext, shareSurface, display);

	pAndroidWebRequestService = new AndroidWebRequestService(*pFileIO, pHttpCache, pTaskQueue, 50);

	pAndroidWebLoadRequestFactory = new AndroidWebLoadRequestFactory(pAndroidWebRequestService, pHttpCache);

	pVehicleModelRepository = new Eegeo::Traffic::VehicleModelRepository;
	pVehicleModelLoader = new Eegeo::Traffic::VehicleModelLoader(pRenderContext->GetGLState(),
																									 *pTextureLoader,
																									 *pFileIO);
	Eegeo::Traffic::VehicleModelLoaderHelper::LoadAllVehicleResourcesIntoRepository(*pVehicleModelLoader, *pVehicleModelRepository);

	pWorld = new Eegeo::EegeoWorld(
		API_KEY,
		pHttpCache,
		pFileIO,
		pTextureLoader,
		pAndroidWebLoadRequestFactory,
		pTaskQueue,
		pVehicleModelRepository,
		*pRenderContext,
		pCameraModel,
		pGlobeCamera,
		pLighting,
		pFogging,
		pMaterialFactory,
		pAndroidLocationService,
		pBlitter,
		pAndroidUrlEncoder,
		*m_pGlobeCameraInterestPointProvider,
		m_androidNativeUIFactories,
		&m_terrainHeightRepository,
		&m_terrainHeightProvider,
		new Eegeo::Search::Service::SearchServiceCredentials("", ""));

	if(!firstTime)
	{
		pGlobeCamera->SetInterestHeadingDistance(lastGlobeCameraLatLong, lastGlobeCameraHeading, lastGlobeCameraDistanceToInterest);
	}
	else
	{
		pAppOnMap = new MyApp(&pInputHandler);
		pInputProcessor = new Eegeo::Android::Input::AndroidInputProcessor(&pInputHandler, pRenderContext->GetScreenWidth(), pRenderContext->GetScreenHeight());

		pGlobeCamera->SetInterestHeadingDistance(Eegeo::Space::LatLongAltitude(51.506172,-0.118915, 0, Eegeo::Space::LatLongUnits::Degrees),
														351.0f,
													   2731.0f);
	}

	pAppOnMap->Start(pWorld);

	firstTime = false;
}
