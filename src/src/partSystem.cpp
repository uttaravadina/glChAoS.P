//------------------------------------------------------------------------------
//  Copyright (c) 2018-2020 Michele Morrone
//  All rights reserved.
//
//  https://michelemorrone.eu - https://BrutPitt.com
//
//  twitter: https://twitter.com/BrutPitt - github: https://github.com/BrutPitt
//
//  mailto:brutpitt@gmail.com - mailto:me@michelemorrone.eu
//
//  This software is distributed under the terms of the BSD 2-Clause license
//------------------------------------------------------------------------------
#include "partSystem.h"


void particlesSystemClass::buildEmitter(enumEmitterEngine ee)
{
    emitter = ee == enumEmitterEngine::emitterEngine_staticParticles ?
                    (emitterBaseClass*) new singleEmitterClass :
                    (emitterBaseClass*) new transformedEmitterClass;

#if !defined(GLCHAOSP_LIGHTVER) || defined(GLCHAOSP_LIGHTVER_EXPERIMENTAL)
    renderBaseClass::create();
#endif

    emitter->buildEmitter(); // post build for WebGL texture ID outRange

//start new thread (if aux thread enabled)
#if !defined(GLCHAOSP_LIGHTVER)
//lock aux thread until initialization is complete
    std::lock_guard<std::mutex> l( attractorsList.getStepMutex() );
#endif
    attractorsList.newStepThread(emitter);
}


void particlesSystemClass::deleteEmitter()
{
    attractorsList.deleteStepThread();
    delete emitter;
}


void particlesSystemClass::changeEmitter(enumEmitterEngine ee)
{

    GLuint circBuffer = emitter->getSizeCircularBuffer();
    bool fStop = emitter->stopFull();
    bool restart = emitter->restartCircBuff();
    deleteEmitter();

    theApp->setEmitterEngineType(ee);

    buildEmitter(ee);
    emitter->setSizeCircularBuffer(circBuffer);
    emitter->stopFull(fStop);
    emitter->restartCircBuff(restart);

}


void particlesSystemClass::onReshape(int w, int h)
{
    if(w==0 || h==0) return; //Is in iconic (GLFW do not intercept on Window)

    getTMat()->setPerspective(float(w)/float(h));

    getRenderFBO().reSizeFBO(w, h);
    getPostRendering()->getFBO().reSizeFBO(w, h);
    getShadow()->resize(w, h);
    getAO()->getFBO().reSizeFBO(w, h);
    shaderPointClass::getGlowRender()->getFBO().reSizeFBO(w, h);
#if !defined(GLCHAOSP_NO_FXAA)
    shaderPointClass::getFXAA()->getFBO().reSizeFBO(w, h);
#endif
#if !defined(GLCHAOSP_LIGHTVER)
    shaderBillboardClass::getGlowRender()->getFBO().reSizeFBO(w, h);
    shaderBillboardClass::getFXAA()->getFBO().reSizeFBO(w, h);
    getMotionBlur()->getFBO().reSizeFBO(w, h);
    getMergedRendering()->getFBO().reSizeFBO(w, h);
#endif
    setFlagUpdate();
}


void particlesSystemClass::renderAxes(transformsClass *model)
{

#if !defined(GLCHAOSP_LIGHTVER)

    transformsClass *axes = getAxes()->getTransforms();

    auto syncAxes = [&] () {
        axes->setView(model->getPOV(), getTMat()->getTGT());
        axes->setPerspective(model->getPerspAngle(), float(theApp->GetWidth())/float(theApp->GetHeight()), model->getPerspNear(), model->getPerspFar() );
        axes->getTrackball().setRotation(model->getTrackball().getRotation());
    };

    if(showAxes() == renderBaseClass::showAxesToSetCoR) {
    //  Set center of rotation
    //////////////////////////////////////////////////////////////////
        syncAxes();

        // no dolly & pan: axes are to center
        axes->getTrackball().setPanPosition(vec3(0.0));
        axes->getTrackball().setDollyPosition(vec3(0.0));

        // get rotation & translation of model w/o pan & dolly
        quat q =   model->getTrackball().getRotation() ;
        model->tM.mMatrix = mat4_cast(q) * translate(mat4(1.f), model->getTrackball().getRotationCenter());
        model->build_MV_MVP();

        // apply rotation to matrix... then subtract prevous model translation
        axes->tM.mMatrix = mat4(1.f);
        axes->getTrackball().applyRotation(axes->tM.mMatrix);
        axes->tM.mMatrix = translate(axes->tM.mMatrix , -model->getTrackball().getRotationCenter());
        axes->build_MV_MVP();


    } else  {
    //  Show center of rotation
    //////////////////////////////////////////////////////////////////
        if(showAxes() == renderBaseClass::showAxesToViewCoR) {
            syncAxes();

            // add RotCent component to dolly & pan... to translate axes with transforms
            vec3 v(model->getTrackball().getRotationCenter());
            axes->getTrackball().setPanPosition(model->getTrackball().getPanPosition()-vec3(v.x, v.y, 0.0));
            axes->getTrackball().setDollyPosition(model->getTrackball().getDollyPosition()-vec3(0.0, 0.0, v.z));

            axes->applyTransforms();
        }

        model->applyTransforms();
    }
#endif
}


GLuint particlesSystemClass::renderSingle()
{
    glViewport(0,0, getWidth(), getHeight());

    GLuint texRendered;

    emitter->preRenderEvents();

#if !defined(GLCHAOSP_LIGHTVER)
    if(getRenderMode() != RENDER_USE_BOTH) {
        texRendered = renderParticles();
        texRendered = renderGlowEffect(texRendered);
        texRendered = renderFXAA(texRendered);
    } else {
        GLuint tex1 = shaderBillboardClass::render(0, getEmitter());
        GLuint tex2 = shaderPointClass::render(1, getEmitter());
        emitter->bufferRendered();

        if(shaderBillboardClass::getFXAA()->isOn()) tex1 = shaderBillboardClass::getFXAA()->render(tex1, true);
        shaderBillboardClass::getGlowRender()->render(tex1, shaderBillboardClass::getGlowRender()->getFBO().getFB(1));

        if(shaderPointClass::getFXAA()->isOn())  tex2 = shaderPointClass::getFXAA()->render(tex2, true);
        shaderPointClass::getGlowRender()->render(tex2, shaderPointClass::getGlowRender()->getFBO().getFB(1));

        texRendered = getMergedRendering()->render(shaderBillboardClass::getGlowRender()->getFBO().getTex(1), shaderPointClass::getGlowRender()->getFBO().getTex(1));  // only if Motionblur

    }
#else
    texRendered = renderParticles();
    texRendered = renderGlowEffect(texRendered);
#if !defined(GLCHAOSP_NO_FXAA)
    if(shaderPointClass::getPtr()->getFXAA()->isOn())
        texRendered = shaderPointClass::getPtr()->getFXAA()->render(texRendered);
#endif
#endif
    emitter->postRenderEvents();
    return texRendered;
}


GLuint particlesSystemClass::renderTF()
{
    const int w = getWidth(), h = getHeight();
    glViewport(0,0, w, h);

    tfSettinsClass &cPit = getParticleRenderPtr()->getTFSettings();

    getEmitter()->preRenderEvents();

    const vec3 vecA(vec3(attractorsList.get()->getCurrent() ));

    const int buffSize = attractorsList.get()->getQueueSize()-1;
    int idx = cPit.getTailPosition()*buffSize+.5;

    const vec3 vecB(vec3(attractorsList.get()->getAt(idx<1 ? 1 : (idx>buffSize ? buffSize : idx))));

    vec3 cpPOV((cPit.invertView() ? vecA : vecB));
    vec3 cpTGT((cPit.invertView() ? vecB : vecA));

    transformsClass *cpTM = getCockPitTMat();

    cpTM->setPerspective(tfSettinsClass::getPerspAngle(), float(w) / float(h),
                         tfSettinsClass::getPerspNear(),
                         getTMat()->getPerspFar());


/*
    const vec3 vecDirH = (cpTGT) + (cpPOV-cpTGT)*cPit.getMovePositionHead();
    const vec3 vecDirT = (cpPOV-cpTGT) + (cpPOV-cpTGT)*cPit.getMovePositionTail();

    mat4 m = translate(mat4(1.f), vecDirH);
    m = m * mat4_cast(cPit.getRotation());
    cpPOV = mat4(m) * vec4(vecDirT, 1.0);
    //cpTGT = mat4(m) * vec4(vecDirH, 1.0);

    cpTM->setView(cpPOV, vecDirH);

*/
    const vec3 vecDirH = (cpPOV-cpTGT) + (cpPOV-cpTGT)*cPit.getMovePositionHead();
    const vec3 vecDirT = (cpPOV-cpTGT) + (cpPOV-cpTGT)*cPit.getMovePositionTail();

    mat4 m = translate(mat4(1.f), cpTGT);
    m = m * mat4_cast(cPit.getRotation());
    cpPOV = mat4(m) * vec4(vecDirT, 1.0);
    cpTGT = mat4(m) * vec4(vecDirH, 1.0);

    cpTM->setView(cpPOV, cpTGT);


    // New settings for tfSettings
    cpTM->getTrackball().setRotation(quat(1.0f,0.0f, 0.0f, 0.0f));
    cpTM->getTrackball().setRotationCenter(vec3(0.f));

    cpTM->getTrackball().setPosition(vec3(0.f));
    cpTM->applyTransforms();

    //Render TF full screen view
    GLuint texRendered = renderParticles(true, !cPit.invertPIP());
    texRendered = renderGlowEffect(texRendered);

    //Render PiP view
    setFlagUpdate();
    if(cPit.getPIPposition() != cPit.pip::noPIP) {
        cPit.setViewport(w,h);

        texRendered = renderParticles(false, cPit.invertPIP());
        texRendered = renderGlowEffect(texRendered);
        glViewport(0,0, w, h);
    }

#if !defined(GLCHAOSP_NO_FXAA)
    texRendered = renderFXAA(texRendered);
#endif

    getEmitter()->postRenderEvents();

    return texRendered;
}