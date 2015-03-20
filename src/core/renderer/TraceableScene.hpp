#ifndef TRACEABLESCENE_HPP_
#define TRACEABLESCENE_HPP_

#include "integrators/Integrator.hpp"

#include "primitives/InfiniteSphere.hpp"
#include "primitives/EmbreeUtil.hpp"
#include "primitives/Primitive.hpp"

#include "materials/ConstantTexture.hpp"

#include "cameras/Camera.hpp"

#include "volume/Medium.hpp"

#include "RendererSettings.hpp"

#include <embree/include/intersector1.h>
#include <embree/geometry/virtual_scene.h>
#include <embree/common/ray.h>
#include <vector>
#include <memory>

namespace Tungsten {

class TraceableScene
{
    struct PerRayData
    {
        IntersectionTemporary &data;
        Ray &ray;
    };

    static void intersect(const void *userData, embree::Ray &eRay)
    {
        const Primitive *primitive = reinterpret_cast<const Primitive *>(userData);
        PerRayData *data = reinterpret_cast<PerRayData *>(eRay.userData);
//      if (primitive->needsRayTransform()) {
//          Ray ray(fromERay(eRay));
//          float length = ray.dir().length();
//          ray.setDir(ray.dir()*(1.0f/length));
//          ray.setNearT(ray.nearT()/length);
//          ray.setFarT(ray.farT()/length);
//          if (primitive->intersect(ray, data->data)) {
//              data->ray.setFarT(ray.farT()*length);
//              eRay.tfar = data->ray.farT();
//          }
//      } else {
            if (primitive->intersect(data->ray, data->data))
                eRay.tfar = data->ray.farT();
//      }
    }

    static bool occluded(const void *userData, embree::Ray &eRay)
    {
        const Primitive *primitive = reinterpret_cast<const Primitive *>(userData);
        return primitive->occluded(EmbreeUtil::convert(eRay));
    }

    const float DefaultEpsilon = 5e-4f;

    Camera &_cam;
    Integrator &_integrator;
    std::vector<std::shared_ptr<Primitive>> &_primitives;
    std::vector<std::shared_ptr<Bsdf>> &_bsdfs;
    std::vector<std::shared_ptr<Medium>> &_media;
    std::vector<std::shared_ptr<Primitive>> _lights;
    std::vector<std::shared_ptr<Primitive>> _infinites;
    std::vector<const Primitive *> _finites;
    RendererSettings _settings;

    embree::VirtualScene *_scene = nullptr;
    embree::Intersector1 *_intersector = nullptr;
    embree::Intersector1 _virtualIntersector;

public:
    TraceableScene(Camera &cam, Integrator &integrator,
            std::vector<std::shared_ptr<Primitive>> &primitives,
            std::vector<std::shared_ptr<Bsdf>> &bsdfs,
            std::vector<std::shared_ptr<Medium>> &media,
            RendererSettings settings,
            uint32 seed)
    : _cam(cam),
      _integrator(integrator),
      _primitives(primitives),
      _bsdfs(bsdfs),
      _media(media),
      _settings(settings)
    {
        _virtualIntersector.intersectPtr = &TraceableScene::intersect;
        _virtualIntersector.occludedPtr = &TraceableScene::occluded;

        _cam.prepareForRender();

        for (std::shared_ptr<Medium> &m : _media)
            m->prepareForRender();

        for (std::shared_ptr<Bsdf> &b : _bsdfs)
            b->prepareForRender();

        int finiteCount = 0, lightCount = 0;
        for (std::shared_ptr<Primitive> &m : _primitives) {
            m->prepareForRender();
            for (int i = 0; i < m->numBsdfs(); ++i)
                if (m->bsdf(i)->unnamed())
                    m->bsdf(i)->prepareForRender();

            if (m->isInfinite()) {
                _infinites.push_back(m);
            } else if (!m->isDelta()) {
                finiteCount++;
            }

            if (m->isEmissive()) {
                lightCount++;
                if (m->isSamplable()) {
                    m->setLightIndex(_lights.size());
                    _lights.push_back(m);
                }
            }
        }
        if (lightCount == 0) {
            std::shared_ptr<InfiniteSphere> defaultLight = std::make_shared<InfiniteSphere>();;
            defaultLight->setEmission(std::make_shared<ConstantTexture>(1.0f));
            defaultLight->setLightIndex(0);
            _lights.push_back(defaultLight);
            _infinites.push_back(defaultLight);
        }

        if (_settings.useSceneBvh()) {
            _scene = new embree::VirtualScene(finiteCount, "bvh2");
            embree::VirtualScene::Object *objects = _scene->objects;
            for (std::shared_ptr<Primitive> &m : _primitives) {
                if (m->isInfinite() || m->isDelta())
                    continue;

                if (m->needsRayTransform()) {
                    objects->hasTransform = true;
                    objects->localBounds = EmbreeUtil::convert(m->bounds());
                    objects->local2world = EmbreeUtil::convert(m->transform());
                    objects->calculateWorldData();
                } else {
                    objects->hasTransform = false;
                    objects->localBounds = objects->worldBounds = EmbreeUtil::convert(m->bounds());
                }

                /* TODO: Transforms */
                objects->userData = m.get();
                objects->intersector1 = &_virtualIntersector;
                objects++;
            }

            embree::rtcBuildAccel(_scene, "objectsplit");
            _intersector = embree::rtcQueryIntersector1(_scene, "fast");
        } else {
            for (std::shared_ptr<Primitive> &m : _primitives)
                if (!m->isInfinite() && !m->isDelta())
                    _finites.push_back(m.get());
        }

        _integrator.prepareForRender(*this, seed);
    }

    ~TraceableScene()
    {
        _integrator.teardownAfterRender();
        _cam.teardownAfterRender();

        for (std::shared_ptr<Medium> &m : _media)
            m->teardownAfterRender();

        for (std::shared_ptr<Bsdf> &b : _bsdfs)
            b->teardownAfterRender();

        for (std::shared_ptr<Primitive> &m : _primitives) {
            m->teardownAfterRender();
            for (int i = 0; i < m->numBsdfs(); ++i)
                if (m->bsdf(i)->unnamed())
                    m->bsdf(i)->teardownAfterRender();
        }

        embree::rtcDeleteGeometry(_scene);
        _scene = nullptr;
        _intersector = nullptr;
    }

    bool intersect(Ray &ray, IntersectionTemporary &data, IntersectionInfo &info) const
    {
        info.primitive = nullptr;
        data.primitive = nullptr;

        if (_settings.useSceneBvh()) {
            PerRayData rayData{data, ray};
            embree::Ray eRay(EmbreeUtil::convert(ray));
            eRay.userData = &rayData;

            _intersector->intersect(eRay);
        } else {
            for (const Primitive *prim : _finites)
                prim->intersect(ray, data);
        }

        if (!data.primitive)
            for (const std::shared_ptr<Primitive> &p : _infinites)
                p->intersect(ray, data);

        if (data.primitive) {
            info.p = ray.pos() + ray.dir()*ray.farT();
            info.w = ray.dir();
//          if (data.primitive->needsRayTransform()) {
//              Vec3f scale = data.primitive->transform().extractScaleVec();
//              float diagScale = scale.avg();
//              info.epsilon = DefaultEpsilon/diagScale;
//              data.primitive->intersectionInfo(data, info);
//              info.epsilon *= diagScale;
//              info.Ng = data.primitive->transform()
//          } else {
                info.epsilon = DefaultEpsilon;
                data.primitive->intersectionInfo(data, info);
//          }
            return true;
        } else {
            return false;
        }
    }

    bool occluded(const Ray &ray) const
    {
        embree::Ray eRay(EmbreeUtil::convert(ray));
        return _intersector->occluded(eRay);
    }

    Camera &cam() const
    {
        return _cam;
    }

    Integrator &integrator() const
    {
        return _integrator;
    }

    const std::vector<std::shared_ptr<Primitive>> &primitives() const
    {
        return _primitives;
    }

    std::vector<std::shared_ptr<Primitive>> &lights()
    {
        return _lights;
    }

    const std::vector<std::shared_ptr<Primitive>> &lights() const
    {
        return _lights;
    }

    const std::vector<std::shared_ptr<Medium>> &media() const
    {
        return _media;
    }

    RendererSettings rendererSettings() const
    {
        return _settings;
    }
};

}

#endif /* TRACEABLESCENE_HPP_ */
