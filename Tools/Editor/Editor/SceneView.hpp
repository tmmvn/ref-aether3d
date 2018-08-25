#pragma once

namespace ae3d
{
    class GameObject;
    struct Vec3;
    
    namespace FileSystem
    {
        struct FileContentsData;
    }
}

struct SceneView;

void svInit( SceneView** sceneView, int width, int height );
void svBeginRender( SceneView* sceneView );
void svEndRender( SceneView* sceneView );
void svLoadScene( SceneView* sceneView, const ae3d::FileSystem::FileContentsData& contents );
void svRotateCamera( SceneView* sceneView, float xDegrees, float yDegrees );
void svMoveCamera( SceneView* sceneView, const ae3d::Vec3& moveDir );
void svMoveSelection( SceneView* sceneView, const ae3d::Vec3& moveDir );
ae3d::GameObject* svSelectObject( SceneView* sceneView, int screenX, int screenY, int width, int height );
