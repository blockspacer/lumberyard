/*
* All or portions of this file Copyright (c) Amazon.com, Inc. or its affiliates or
* its licensors.
*
* For complete copyright and license terms please see the LICENSE at the root of this
* distribution (the "License"). All use of this software is governed by the License,
* or, if provided, by the license below or the license accompanying this file. Do not
* remove or modify any license notices. This file is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*
*/
// Original file Copyright Crytek GMBH or its affiliates, used under license.

#include "CryLegacy_precompiled.h"

#include <I3DEngine.h>
#include <ICryAnimation.h>
#include "ModelMesh.h"
#include "ModelSkin.h"
#include "Model.h"
#include "LoaderCHR.h"
#include "FacialAnimation/FaceAnimation.h"
#include "FacialAnimation/FacialModel.h"
#include "FacialAnimation/FaceEffectorLibrary.h"
#include "AnimEventLoader.h"
#include "CharacterManager.h"
#include "StringUtils.h"

//////////////////////////////////////////////////////////////////////////
// temporary solution to access the links for thin and fat models
//////////////////////////////////////////////////////////////////////////

namespace CryCHRLoader_LoadNewCHR_Helpers
{
    static bool FindFirstMesh(CMesh*& pMesh, CNodeCGF*& pGFXNode, CContentCGF* pContent, const char* filenameNoExt, int lod)
    {
        uint32 numNodes = pContent->GetNodeCount();
        pGFXNode = 0;
        for (uint32 n = 0; n < numNodes; n++)
        {
            CNodeCGF* pNode = pContent->GetNode(n);
            if (pNode->type == CNodeCGF::NODE_MESH && pNode->pMesh)
            {
                pGFXNode = pNode;
                break;
            }
        }
        if (pGFXNode == 0)
        {
            return false;
        }

        pMesh = pGFXNode->pMesh;
        if (pMesh && pMesh->m_pBoneMapping == 0)
        {
            g_pISystem->Warning(VALIDATOR_MODULE_ANIMATION, VALIDATOR_WARNING, VALIDATOR_FLAG_FILE, filenameNoExt,   "Failed to Load Character file. Skeleton-Initial-Positions are missing");
            return false;
        }

        return true;
    }
}

bool CryCHRLoader::BeginLoadCHRRenderMesh(CDefaultSkeleton* pSkel, EStreamTaskPriority estp)
{
    using namespace CryCHRLoader_LoadNewCHR_Helpers;

    const char* szFilePath = pSkel->GetModelFilePath();

    LOADING_TIME_PROFILE_SECTION(g_pISystem);

    COMPILE_TIME_ASSERT(sizeof(TFace) == 6);

    CRY_DEFINE_ASSET_SCOPE(CRY_SKEL_FILE_EXT, szFilePath);

    const char* szExt = CryStringUtils::FindExtension(szFilePath);
    m_strGeomFileNameNoExt.assign (szFilePath, *szExt ? szExt - 1 : szExt);

    if (m_strGeomFileNameNoExt.empty())
    {
        g_pISystem->Warning(VALIDATOR_MODULE_ANIMATION, VALIDATOR_WARNING, VALIDATOR_FLAG_FILE, szFilePath, "Wrong character filename");
        return false;
    }

    //--------------------------------------------------------------------------------------------

    stack_string filename = stack_string(m_strGeomFileNameNoExt.c_str()) + "." + szExt;

    CModelMesh& mm = *pSkel->GetModelMesh();

    string lodName = filename;
    if (mm.m_stream.bHasMeshFile)
    {
        lodName += 'm';
    }

    m_pModelSkel = pSkel;

    StreamReadParams params;
    params.nFlags = 0;//IStreamEngine::FLAGS_NO_SYNC_CALLBACK;
    params.dwUserData = 0;
    params.ePriority = estp;
    IStreamEngine* pStreamEngine = gEnv->pSystem->GetStreamEngine();
    m_pStream = pStreamEngine->StartRead(eStreamTaskTypeAnimation, lodName.c_str(), this, &params);

    return m_pStream != NULL;
}

void CryCHRLoader::AbortLoad()
{
    if (m_pStream)
    {
        m_pStream->Abort();
    }
}

void CryCHRLoader::StreamAsyncOnComplete (IReadStream* pStream, unsigned nError)
{
    if (!nError)
    {
        if (m_pModelSkel)
        {
            EndStreamSkel(pStream);
        }
        else if (m_pModelSkin)
        {
            EndStreamSkinAsync(pStream);
        }
    }

    pStream->FreeTemporaryMemory();
}

void CryCHRLoader::EndStreamSkel(IReadStream* pStream)
{
    using namespace CryCHRLoader_LoadNewCHR_Helpers;

    const char* lodName = pStream->GetName();

    SmartContentCGF pCGF = g_pI3DEngine->CreateChunkfileContent(lodName);
    bool bLoaded = g_pI3DEngine->LoadChunkFileContentFromMem(pCGF, pStream->GetBuffer(), pStream->GetBytesRead(), IStatObj::ELoadingFlagsJustGeometry, false, false);

    if (!bLoaded || !pCGF)
    {
        g_pISystem->Warning(VALIDATOR_MODULE_ANIMATION, VALIDATOR_WARNING, VALIDATOR_FLAG_FILE, lodName, "%s: The Chunk-Loader failed to load the file.", __FUNCTION__);
        return;
    }

    CContentCGF* pLodContent = pCGF;

    CNodeCGF* pMeshNode = 0;
    CMesh* pMesh = 0;
    if (!FindFirstMesh(pMesh, pMeshNode, pLodContent, m_strGeomFileNameNoExt.c_str(), 0))
    {
        return;
    }

    CModelMesh* pModelMesh = m_pModelSkel->GetModelMesh();
    m_pNewRenderMesh = pModelMesh->InitRenderMeshAsync(pMesh, m_pModelSkel->GetModelFilePath(), 0, m_arrNewRenderChunks);
}

void CryCHRLoader::StreamOnComplete (IReadStream* pStream, unsigned nError)
{
    if (m_pModelSkel)
    {
        if (CModelMesh* pModelMesh = m_pModelSkel->GetModelMesh())
        {
            pModelMesh->InitRenderMeshSync(m_arrNewRenderChunks, m_pNewRenderMesh);
            m_pNewRenderMesh = NULL;
            m_arrNewRenderChunks.clear();
        }

        m_pModelSkel->m_ModelMesh.m_stream.pStreamer = NULL;
    }
    else if (m_pModelSkin)
    {
        EndStreamSkinSync(pStream);

        int nRenderLod = (int)pStream->GetParams().dwUserData;
        m_pModelSkin->m_arrModelMeshes[nRenderLod].m_stream.pStreamer = NULL;
    }
    m_pStream = NULL;
    delete this;
}



// cleans up the resources allocated during load
void CryCHRLoader::ClearModel()
{
    m_strGeomFileNameNoExt = "";
    //  m_animListIDs.clear();
}





























//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----              this belongs into another file (i.e.CDefaultSkeleton)   -------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
namespace SkelLoader_Helpers
{
    struct LevelOfDetail
    {
        CContentCGF* m_pContentCGF;
        CContentCGF* m_pContentMeshCGF;

        LevelOfDetail()
        {
            m_pContentCGF = 0;
            m_pContentMeshCGF = 0;
        }

        ~LevelOfDetail()
        {
            if (m_pContentCGF)
            {
                g_pI3DEngine->ReleaseChunkfileContent(m_pContentCGF), m_pContentCGF = 0;
            }
            if (m_pContentMeshCGF)
            {
                g_pI3DEngine->ReleaseChunkfileContent(m_pContentMeshCGF), m_pContentMeshCGF = 0;
            }
        }
    };



    static bool InitializeBones(CDefaultSkeleton* pDefaultSkeleton, CSkinningInfo* pSkinningInfo, const char* szFilePath)
    {
        const int lod = 0;
        uint32 numBones = pSkinningInfo->m_arrBonesDesc.size();
        CRY_ASSERT(numBones);
        if (numBones >= MAX_JOINT_AMOUNT)
        {
            g_pISystem->Warning(VALIDATOR_MODULE_ANIMATION, VALIDATOR_WARNING, VALIDATOR_FLAG_FILE, szFilePath, "Too many Joints in model. Current Limit is: %d", MAX_JOINT_AMOUNT);
            return false;
        }

        if (!numBones)
        {
            g_pISystem->Warning(VALIDATOR_MODULE_ANIMATION, VALIDATOR_WARNING, VALIDATOR_FLAG_FILE, szFilePath,     "Model has no Joints.");
            return false;
        }

        pDefaultSkeleton->m_poseDefaultData.Initialize(numBones);
        pDefaultSkeleton->m_arrModelJoints.resize(numBones);

        for (uint32 nBone = 0; nBone < numBones; ++nBone)
        {
            const CryBoneDescData& boneDesc = pSkinningInfo->m_arrBonesDesc[nBone];
            CRY_ASSERT(boneDesc.m_DefaultB2W.IsOrthonormalRH());
            if (!boneDesc.m_DefaultB2W.IsOrthonormalRH())
            {
                g_pISystem->Warning(VALIDATOR_MODULE_ANIMATION, VALIDATOR_WARNING, VALIDATOR_FLAG_FILE, szFilePath, "Bone %d: B2W is not orthonormal RH in lod %d", nBone, lod);
                return false;
            }

            CDefaultSkeleton::SJoint& joint = pDefaultSkeleton->m_arrModelJoints[nBone];
            joint.m_idxParent   = -1;
            int32 offset = boneDesc.m_nOffsetParent;
            if (offset)
            {
                joint.m_idxParent = int32(nBone + offset);
            }

            joint.m_strJointName      = &boneDesc.m_arrBoneName[0];
            joint.m_nJointCRC32Lower  = CCrc32::ComputeLowercase(&boneDesc.m_arrBoneName[0]);
            joint.m_nJointCRC32       = boneDesc.m_nControllerID;
#if ENABLE_CRY_PHYSICS
            joint.m_PhysInfo          = boneDesc.m_PhysInfo[0];
#endif
            joint.m_fMass             = boneDesc.m_fMass;

            pDefaultSkeleton->m_poseDefaultData.GetJointsAbsolute()[nBone] = QuatT(boneDesc.m_DefaultB2W);
            pDefaultSkeleton->m_poseDefaultData.GetJointsAbsolute()[nBone].q.Normalize();
            pDefaultSkeleton->m_poseDefaultData.GetJointsRelative()[nBone] = pDefaultSkeleton->m_poseDefaultData.GetJointsAbsolute()[nBone];
            if (joint.m_idxParent >= 0)
            {
                pDefaultSkeleton->m_poseDefaultData.GetJointsRelative()[nBone] =    pDefaultSkeleton->m_poseDefaultData.GetJointsAbsolute()[joint.m_idxParent].GetInverted() * pDefaultSkeleton->m_poseDefaultData.GetJointsAbsolute()[nBone];
            }
            pDefaultSkeleton->m_poseDefaultData.GetJointsRelative()[nBone].q.Normalize();
        }


        uint32 nRootCounter = 0;
        for (uint32 j = 0; j < numBones; j++)
        {
            int32 idxParent = pDefaultSkeleton->m_arrModelJoints[j].m_idxParent;
            if (idxParent < 0)
            {
                nRootCounter++; //count the root joints
            }
        }
        if (nRootCounter != 1)
        {
#if ENABLE_CRY_PHYSICS
            //something went wrong. this model has multiple roots
            for (uint32 j = 0; j < numBones; j++)
            {
                pDefaultSkeleton->m_arrModelJoints[j].m_PhysInfo.pPhysGeom = 0; //delete all "Chunk-IDs" to prevent the FatalError in the destructor.
            }
#endif
            g_pISystem->Warning(VALIDATOR_MODULE_ANIMATION, VALIDATOR_WARNING, VALIDATOR_FLAG_FILE, szFilePath, "CryAnimation: multiple root in skeleton. Please fix the model.");
            return false;
        }

        const char* RootName = pDefaultSkeleton->m_arrModelJoints[0].m_strJointName.c_str();
        if (RootName[0] == 'B' && RootName[1] == 'i' && RootName[2] == 'p' && RootName[3] == '0' && RootName[4] == '1')
        {
            //This character-model is a biped and it was made in CharacterStudio.
            //CharacterStudio is not allowing us to project the Bip01 on the ground and change its orientation; thats why we have to do it here.
            pDefaultSkeleton->m_poseDefaultData.GetJointsAbsolute()[0].SetIdentity();
            pDefaultSkeleton->m_poseDefaultData.GetJointsRelative()[0].SetIdentity();
            uint32 numJoints = pDefaultSkeleton->m_arrModelJoints.size();
            for (uint32 bone = 1; bone < numJoints; bone++)
            {
                //adjust the pelvis (and all other joints directly linked Bip01)
                int32 idxParent = pDefaultSkeleton->m_arrModelJoints[bone].m_idxParent;
                if (idxParent == 0)
                {
                    pDefaultSkeleton->m_poseDefaultData.GetJointsRelative()[bone] = pDefaultSkeleton->m_poseDefaultData.GetJointsAbsolute()[bone];
                }
            }
        }

        return true;
    }

    static bool FindFirstMesh(CMesh*& pMesh, CNodeCGF*& pGFXNode, CContentCGF* pContent, const char* filenameNoExt, int lod)
    {
        uint32 numNodes = pContent->GetNodeCount();
        pGFXNode = 0;
        for (uint32 n = 0; n < numNodes; n++)
        {
            CNodeCGF* pNode = pContent->GetNode(n);
            if (pNode->type == CNodeCGF::NODE_MESH && pNode->pMesh)
            {
                pGFXNode = pNode;
                break;
            }
        }
        if (pGFXNode == 0)
        {
            return false;
        }

        pMesh = pGFXNode->pMesh;
        if (pMesh && pMesh->m_pBoneMapping == 0)
        {
            g_pISystem->Warning(VALIDATOR_MODULE_ANIMATION, VALIDATOR_WARNING, VALIDATOR_FLAG_FILE, filenameNoExt,   "Failed to Load Character file. Skeleton-Initial-Positions are missing");
            return false;
        }

        return true;
    }

    static bool FindFirstMeshAndMaterial(CMesh*& pMesh, CNodeCGF*& pGFXNode, _smart_ptr<IMaterial>& pMaterial, CContentCGF* pContent, const char* filenameNoExt, int lod)
    {
        uint32 numNodes = pContent->GetNodeCount();
        pGFXNode = 0;
        for (uint32 n = 0; n < numNodes; n++)
        {
            CNodeCGF* pNode = pContent->GetNode(n);
            if (pNode->type == CNodeCGF::NODE_MESH)
            {
                pGFXNode = pNode;
                break;
            }
        }
        if (pGFXNode == 0)
        {
            return false;
        }

        pMesh = pGFXNode->pMesh;
        if (pMesh && pMesh->m_pBoneMapping == 0)
        {
            g_pISystem->Warning(VALIDATOR_MODULE_ANIMATION, VALIDATOR_WARNING, VALIDATOR_FLAG_FILE, filenameNoExt,   "Failed to Load Character file. Skeleton-Initial-Positions are missing");
            return false;
        }

        if (lod == 0 || pMaterial == NULL)
        {
            if (pGFXNode->pMaterial)
            {
                pMaterial = g_pI3DEngine->GetMaterialManager()->LoadCGFMaterial(pGFXNode->pMaterial, PathUtil::GetPath(filenameNoExt));
            }
            else
            {
                pMaterial = g_pI3DEngine->GetMaterialManager()->GetDefaultMaterial();
            }
        }
        return true;
    }
}




bool CDefaultSkeleton::LoadNewSKEL(const char* szFilePath, uint32 nLoadingFlags)
{
    using namespace SkelLoader_Helpers;

    LOADING_TIME_PROFILE_SECTION(g_pISystem);

    COMPILE_TIME_ASSERT(sizeof(TFace) == 6);

    CRY_DEFINE_ASSET_SCOPE(CRY_SKEL_FILE_EXT, szFilePath);

    const char* szExt = CryStringUtils::FindExtension(szFilePath);
    stack_string strGeomFileNameNoExt;
    strGeomFileNameNoExt.assign (szFilePath, *szExt ? szExt - 1 : szExt);
    if (strGeomFileNameNoExt.empty())
    {
        g_pISystem->Warning(VALIDATOR_MODULE_ANIMATION, VALIDATOR_WARNING, VALIDATOR_FLAG_FILE, szFilePath, "Wrong character filename");
        return 0;
    }

    //--------------------------------------------------------------------------------------------
#ifndef _RELEASE
    {
        uint32 isSKEL = _stricmp(szExt, CRY_SKEL_FILE_EXT) == 0;
        if (isSKEL)
        {
            string strFilePathLOD1 = strGeomFileNameNoExt + "_lod1" + "." + szExt;
            bool exist = g_pISystem->GetIPak()->IsFileExist(strFilePathLOD1.c_str());
            if (exist)
            {
                g_pISystem->Warning(VALIDATOR_MODULE_ANIMATION, VALIDATOR_WARNING, VALIDATOR_FLAG_FILE, 0, "!CryAnimation: LODs in CHRs are not supported any more. Please remove them from the build: %s", strFilePathLOD1.c_str());
            }
        }
    }
#endif


    //--------------------------------------------------------------------------------------------

    LevelOfDetail cgfs; //in case of early exit we call the destructor automatically and delete all CContentCGFs
    cgfs.m_pContentCGF = g_pI3DEngine->CreateChunkfileContent(szFilePath);
    if (cgfs.m_pContentCGF == 0)
    {
        CryFatalError("CryAnimation error: issue in Chunk-Loader");
    }
    bool bLoaded = g_pI3DEngine->LoadChunkFileContent(cgfs.m_pContentCGF, szFilePath, 0);
    if (bLoaded == 0)
    {
        g_pISystem->Warning(VALIDATOR_MODULE_ANIMATION, VALIDATOR_WARNING, VALIDATOR_FLAG_FILE, szFilePath, "CryAnimation: The Chunk-Loader failed to load the file.");
        return 0;
    }

    if (!Console::GetInst().ca_StreamCHR)
    {
        string lodmName = string(szFilePath) + "m";
        bool bHasMeshFile = gEnv->pCryPak->IsFileExist(lodmName.c_str());
        if (bHasMeshFile)
        {
            cgfs.m_pContentMeshCGF = g_pI3DEngine->CreateChunkfileContent(lodmName);
            g_pI3DEngine->LoadChunkFileContent(cgfs.m_pContentMeshCGF, lodmName.c_str());
        }
    }





    //----------------------------------------------------------------------------------------------------------------------
    //--- initialize the ModelSkel
    //----------------------------------------------------------------------------------------------------------------------
    if (cgfs.m_pContentCGF == 0)
    {
        CryFatalError("CryAnimation error: initialization of model failed: %s", szFilePath);
    }

    PREFAST_ASSUME(cgfs.m_pContentCGF); //cryfatalerror catches it above
    CSkinningInfo* pSkinningInfo = cgfs.m_pContentCGF->GetSkinningInfo();
    if (pSkinningInfo == 0)
    {
        g_pISystem->Warning(VALIDATOR_MODULE_ANIMATION, VALIDATOR_WARNING, VALIDATOR_FLAG_FILE, szFilePath, "CryAnimation: Skinning info is missing in lod %d", 0);
        return 0;
    }


    //--------------------------------------------------------------------------
    //----   initialize the default animation skeleton
    //--------------------------------------------------------------------------
    uint32 isSkeletonValid = InitializeBones(this, pSkinningInfo, szFilePath);

    CNodeCGF* pMeshNode = 0;
    CMesh* pMesh = 0;
    _smart_ptr<IMaterial> pMaterial;
    FindFirstMeshAndMaterial(pMesh, pMeshNode, pMaterial, cgfs.m_pContentCGF, strGeomFileNameNoExt.c_str(), 0);

    //------------------------------------------------------------------------------------
    //--- initialize Physical Proxies
    //------------------------------------------------------------------------------------
    if (1)
    {
        uint32 numJoints = m_arrModelJoints.size();
#if ENABLE_CRY_PHYSICS
        m_arrBackupPhysInfo.resize(numJoints);
        for (uint32 j = 0; j < numJoints; j++)
        {
            m_arrBackupPhysInfo[j] = m_arrModelJoints[j].m_PhysInfo;
        }
#endif // ENABLE_CRY_PHYSICS
        m_arrBackupPhyBoneMeshes = pSkinningInfo->m_arrPhyBoneMeshes;
        m_arrBackupBoneEntities  = pSkinningInfo->m_arrBoneEntities;
#if ENABLE_CRY_PHYSICS
        if (!SetupPhysicalProxies(m_arrBackupPhyBoneMeshes, m_arrBackupBoneEntities, pMaterial, szFilePath))
#endif
        {
            return 0;
        }
    }
    if (isSkeletonValid == 0)
    {
        return 0;
    }

    //------------------------------------------------------------------------------------

    VerifyHierarchy();

    if (1)
    {
        //----------------------------------------------------------------------------------------------------------------------------------
        //---  initialize ModelMesh MorphTargets and RenderMesh
        //---  There can be only one Render LOD in the Base-Model (Even this is optional. Usually the Base-Model has an empty skeleton)
        //----------------------------------------------------------------------------------------------------------------------------------

        if (cgfs.m_pContentMeshCGF)
        {
            if (!FindFirstMesh(pMesh, pMeshNode, cgfs.m_pContentMeshCGF, strGeomFileNameNoExt.c_str(), 0))
            {
                return 0;
            }
        }


        if (pMesh)
        {
            CModelMesh* pModelMesh = GetModelMesh();
            uint32 success = pModelMesh->InitMesh(pMesh, pMeshNode, pMaterial, pSkinningInfo, m_strFilePath.c_str(), 0);
            if (success == 0)
            {
                return 0;
            }

            string meshFilename = string(szFilePath) + 'm';
            pModelMesh->m_stream.bHasMeshFile = gEnv->pCryPak->IsFileExist(meshFilename.c_str());
        }
    }

    //---------------------------------------------------------------------------------------------------------------

    InitializeHardcodedJointsProperty();
    PrepareJointIDHash();
    stack_string paramFileName = strGeomFileNameNoExt + "." + CRY_CHARACTER_PARAM_FILE_EXT;
    uint32 isPrevMode = nLoadingFlags & CA_PreviewMode;
    if (isPrevMode == 0)
    {
        LoadCHRPARAMS(paramFileName);
    }

    m_animListIDs.clear();
    return 1;
}


void CDefaultSkeleton::LoadCHRPARAMS(const char* paramFileName, const std::string* optionalBuffer)
{
    CParamLoader& paramLoader = g_pCharacterManager->GetParamLoader();
    bool ok = false;
    if (optionalBuffer)
    {
        // this will be refilled by LoadXML
        m_animListIDs.clear();
        ok = paramLoader.LoadXML(this, GetDefaultAnimDir(), paramFileName, m_animListIDs, optionalBuffer);
    }
    else
    {
        // If the new .params file exists, load it since it can be done much faster and the file is cleaner (all XML)
        if (g_pIPak->IsFileExist(paramFileName))
        {
            // this will be refilled by LoadXML
            m_animListIDs.clear();
            ok = paramLoader.LoadXML(this, GetDefaultAnimDir(), paramFileName, m_animListIDs);
        }
    }

    if (ok)
    {
        LoadAnimations(paramLoader);
        UpdateAnimationDynamicControllers();
    }
}


bool CDefaultSkeleton::IsAnimInParamFilters(const char * animationPath, CParamLoader& paramLoader, uint32& animListIDOut)
{
    const int32 nListIDs = m_animListIDs.size();
    for (int32 i = 0 ; i < nListIDs; ++i)
    {
        if (paramLoader.IsAnimationWithinFilters(animationPath, m_animListIDs[i]))
        {
            animListIDOut = m_animListIDs[i];
            return true;
        }
    }
    return false;
}

bool CDefaultSkeleton::AddAnimationIfInAnimationSetFilters(const char* animationPath)
{
    const char* pFilePath = GetModelFilePath();
    const char* szExt = CryStringUtils::FindExtension(pFilePath);
    stack_string strGeomFileNameNoExt;
    strGeomFileNameNoExt.assign(pFilePath, *szExt ? szExt - 1 : szExt);
    stack_string paramFileName = strGeomFileNameNoExt + "." + CRY_CHARACTER_PARAM_FILE_EXT;


    // If the new .chrparams file exists, load it since it can be done much faster and the file is cleaner (all XML)
    if (g_pIPak->IsFileExist(paramFileName))
    {
        // this will be refilled by LoadXML
        m_animListIDs.clear();

        CParamLoader& paramLoader = g_pCharacterManager->GetParamLoader();

        bool ok = paramLoader.LoadXML(this, GetDefaultAnimDir(), paramFileName, m_animListIDs);
        if (ok)
        {
            uint32 animListIDOut;
            if( IsAnimInParamFilters(animationPath, paramLoader, animListIDOut))
            {
                LoadAnimationFile(animationPath, paramLoader, animListIDOut);
                stack_string animName = PathUtil::GetFileName(animationPath);
                m_pAnimationSet->NotifyListenersAboutFileAdded(animName.c_str(), animationPath);
                return true;
            }
        }
        else
        {
            gEnv->pLog->LogError("CryAnimation: Unable to load %s.", paramFileName.c_str());
        }
    }
    return false;
}

bool CDefaultSkeleton::RemoveAnimation(const char* animationPath)
{
#ifdef EDITOR_PCDEBUGCODE // Editor-only functionality.
    uint32 animCrc = 0;

    // Remove from the character's animation set.
    if (m_pAnimationSet->RemoveFile(animationPath, animCrc))
    {
        // Remove from the param loader registry, which is applied to newly-initialized (or re-initialized) characters.
        CParamLoader& paramLoader = g_pCharacterManager->GetParamLoader();
        const int32 nListIDs = m_animListIDs.size();
        bool removed = false;
        for (int32 i = nListIDs - 1; i >= 0 && !removed; --i)
        {
            auto& list = paramLoader.GetParsedListWritable(i);
            for (auto iter = list.arrLoadedAnimFiles.begin(); iter != list.arrLoadedAnimFiles.end(); ++iter)
            {
                if (iter->m_CRC32Name == animCrc)
                {
                    list.arrLoadedAnimFiles.erase(iter);
                    removed = true;
                    break;
                }
            }
        }

        return true;
    }
   
#endif // EDITOR_PCDEBUGCODE

    return false;
}

bool CDefaultSkeleton::CreateCHRPARAMS(const char* paramFileName)
{
    if (!g_pIPak->IsFileExist(paramFileName))
    {
        auto paramLoader = g_pCharacterManager->GetParamLoader();
        return paramLoader.WriteXML(paramFileName);
    }
    return false;
}


//------------------------------------------------------------------------------------------------------------
// loads animations for a model by composing a complete animation list and loading it into an AnimationSet
//------------------------------------------------------------------------------------------------------------
bool CDefaultSkeleton::LoadAnimations(CParamLoader& paramLoader)
{
    LOADING_TIME_PROFILE_SECTION(g_pISystem);

    // the number of animations loaded
    uint32 numAnimAssets = 0;

    int32 numAnims = 0;

    FUNCTION_PROFILER(gEnv->pSystem, PROFILE_ANIMATION);

    if (m_animListIDs.size() == 0)
    {
        return false;
    }

    // attributes of the compound list
    // all those attributes have to be stored on a per list basis,
    // keeping it for the topmost list in memory is not enough
    // since another list might use it as a dependency which does not
    // override it
    DynArray<string> modelTracksDatabases;
    DynArray<string> lockedDatabases;
    string animEventDatabase;
    string faceLibFile;
    string faceLibDir;
    CAnimationSet::FacialAnimationSet::container_type facialAnimations;

    const int32 nListIDs = m_animListIDs.size();
    for (int32 i = nListIDs - 1; i >= 0; --i) // by walking backwards the 'parent' lists come first
    {
        const uint32 animListID = m_animListIDs[i];
        if (animListID < paramLoader.GetParsedListNumber())
        {
            const SAnimListInfo& animList = paramLoader.GetParsedList(animListID);
            numAnims += animList.arrAnimFiles.size();
            modelTracksDatabases.push_back(animList.modelTracksDatabases);
            lockedDatabases.push_back(animList.lockedDatabases);

            if (animEventDatabase.empty())
            {
                animEventDatabase = animList.animEventDatabase; // first one wins --> so outermost animeventdb wins
            }

            if (faceLibFile.empty())
            {
                faceLibFile = animList.faceLibFile;
                faceLibDir = animList.faceLibDir;
            }
        }
    }

    m_pAnimationSet->prepareLoadCAFs (numAnims);

    uint32 numTracksDataBases = modelTracksDatabases.size();
    if (numTracksDataBases)
    {
        g_AnimationManager.CreateGlobalHeaderDBA(modelTracksDatabases);
    }

    // Lock Persistent Databases
    DynArray<string>::iterator it;
    for (it = lockedDatabases.begin(); it != lockedDatabases.end(); ++it)
    {
        g_AnimationManager.DBA_LockStatus(it->c_str(), 1, false);
    }

    if (!animEventDatabase.empty())
    {
        SetModelAnimEventDatabase(animEventDatabase);
    }

    if (!faceLibFile.empty())
    {
        CreateFacialInstance();
        LoadFaceLib(faceLibFile, faceLibDir, this);
    }

    for (int32 i = nListIDs - 1; i >= 0; --i)
    {
        const SAnimListInfo& animList = paramLoader.GetParsedList(m_animListIDs[i]);
        uint32 numAnimNames = animList.arrAnimFiles.size();

        // this is where the Animation List Cache kicks in
        if (!animList.headersLoaded)
        {
            paramLoader.ExpandWildcards(m_animListIDs[i]);
            numAnimAssets += LoadAnimationFiles(paramLoader, m_animListIDs[i]);
            // this list has been processed, dont do it again!
            paramLoader.SetHeadersLoaded(m_animListIDs[i]);
        }
        else
        {
            numAnimAssets += ReuseAnimationFiles(paramLoader, m_animListIDs[i]);
        }

        if (!animList.facialAnimations.empty())
        {
            facialAnimations.insert(facialAnimations.end(), animList.facialAnimations.begin(), animList.facialAnimations.end());
        }
    }

    // Store the list of facial animations in the model animation set.
    if (!facialAnimations.empty())
    {
        m_pAnimationSet->GetFacialAnimations().SwapElementsWithVector(facialAnimations);
    }

    AnimEventLoader::LoadAnimationEventDatabase(this, GetModelAnimEventDatabaseCStr());

    uint32 dddd = g_AnimationManager.m_arrGlobalCAF.size();

    if (numAnimAssets > 1)
    {
        g_pILog->UpdateLoadingScreen("  %d animation-assets loaded (total assets: %d)", numAnimAssets, g_AnimationManager.m_arrGlobalCAF.size());
    }

    // If there is a facial model, but no expression library, we should create an empty library for it.
    // When we assign the library to the facial model it will automatically be assigned the morphs as expressions.
    {
        CFacialModel* pFacialModel = GetFacialModel();
        CFacialEffectorsLibrary* pEffectorsLibrary = (pFacialModel ? pFacialModel->GetFacialEffectorsLibrary() : 0);
        if (pFacialModel && !pEffectorsLibrary)
        {
            CFacialAnimation* pFacialAnimationManager = (g_pCharacterManager ? g_pCharacterManager->GetFacialAnimation() : 0);
            CFacialEffectorsLibrary* pNewLibrary = new CFacialEffectorsLibrary(pFacialAnimationManager);
            const string& filePath = GetModelFilePath();
            if (!filePath.empty())
            {
                string libraryFilePath = PathUtil::ReplaceExtension(filePath, ".fxl");
                pNewLibrary->SetName(libraryFilePath);
            }
            if (pFacialModel && pNewLibrary)
            {
                pFacialModel->AssignLibrary(pNewLibrary);
            }
        }
    }

    if (Console::GetInst().ca_PrecacheAnimationSets)
    {
        CAnimationSet& animSet = *m_pAnimationSet;
        uint32 animCount = animSet.GetAnimationCount();
        for (uint32 i = 0; i < animCount; ++i)
        {
            const ModelAnimationHeader* pHeader = animSet.GetModelAnimationHeader(i);
            if (pHeader)
            {
                if (pHeader->m_nAssetType == CAF_File)
                {
                    uint32 globalID = pHeader->m_nGlobalAnimId;
                    if (globalID > 0)
                    {
                        PREFAST_ASSUME(g_pCharacterManager);
                        g_AnimationManager.m_arrGlobalCAF[globalID].StartStreamingCAF();
                    }
                }
            }
        }
    }

    m_pAnimationSet->VerifyLMGs();

    CModelMesh* pModelMesh = GetModelMesh();
    return m_arrModelJoints.size() != 0;
}




void CDefaultSkeleton::LoadFaceLib(const char* faceLibFile, const char* animDirName, CDefaultSkeleton* pDefaultSkeleton)
{
    const char* pFilePath = GetModelFilePath();
    const char* szExt = CryStringUtils::FindExtension(pFilePath);
    stack_string strGeomFileNameNoExt;
    strGeomFileNameNoExt.assign (pFilePath, *szExt ? szExt - 1 : szExt);

    LOADING_TIME_PROFILE_SECTION(g_pISystem);
    // Facial expressions library.
    _smart_ptr<IFacialEffectorsLibrary> pLib;

    pLib = g_pCharacterManager->GetIFacialAnimation()->LoadEffectorsLibrary(faceLibFile);
    if (!pLib)
    {
        // Search in animation directory .chr file directory.
        pLib = g_pCharacterManager->GetIFacialAnimation()->LoadEffectorsLibrary(
                PathUtil::Make(animDirName, faceLibFile));
    }
    if (!pLib)
    {
        // Search in .chr file directory.
        pLib = g_pCharacterManager->GetIFacialAnimation()->LoadEffectorsLibrary(
                PathUtil::Make(PathUtil::GetParentDirectory(strGeomFileNameNoExt), faceLibFile));
    }
    if (pLib)
    {
        if (pDefaultSkeleton->GetFacialModel())
        {
            pDefaultSkeleton->GetFacialModel()->AssignLibrary(pLib);
        }
    }
    else
    {
        gEnv->pLog->LogError("CryAnimation: Facial Effector Library %s not found (when loading %s.chrparams)", faceLibFile, strGeomFileNameNoExt.c_str());
    }
}

//-------------------------------------------------------------------------------------------------
#define MAX_STRING_LENGTH (256)

uint32 CDefaultSkeleton::ReuseAnimationFiles(CParamLoader& paramLoader, uint32 listID)
{
    LOADING_TIME_PROFILE_SECTION(g_pISystem);

    const SAnimListInfo& animList = paramLoader.GetParsedList(listID);
    int32 numAnims = animList.arrLoadedAnimFiles.size();
    uint32 result = 0;

    m_pAnimationSet->PrepareToReuseAnimations(numAnims);

    for (int i = 0; i < numAnims; ++i)
    {
        const ModelAnimationHeader& animHeader = animList.arrLoadedAnimFiles[i];

        m_pAnimationSet->ReuseAnimation(animHeader);
        result++;
    }

    return result;
}

uint32 CDefaultSkeleton::LoadAnimationFiles(CParamLoader& paramLoader, uint32 listID)
{
    const char* pFullFilePath = GetModelFilePath();
    const char* szExt = CryStringUtils::FindExtension(pFullFilePath);
    stack_string strGeomFileNameNoExt;
    strGeomFileNameNoExt.assign (pFullFilePath, *szExt ? szExt - 1 : szExt);

    LOADING_TIME_PROFILE_SECTION(g_pISystem);
    // go through all Anims on stack and load them
    uint32 nAnimID = 0;
    int32 numAnims = 0;

    const SAnimListInfo& animList = paramLoader.GetParsedList(listID);


#if BLENDSPACE_VISUALIZATION
    const char* strInternalType_Para1D = "InternalPara1D";
    int32 nDefAnimID = m_pAnimationSet->GetAnimIDByName(strInternalType_Para1D);
    if (nDefAnimID < 0)
    {
        int32 nGAnimID = m_pAnimationSet->LoadFileLMG("InternalPara1D.LMG", strInternalType_Para1D);
        if (nGAnimID)
        {
            CryFatalError("The GlobalID of the default Para1D must be 0");
        }
        int nAnimID0 = m_pAnimationSet->GetAnimIDByName(strInternalType_Para1D);
        if (nAnimID0 >= 0)
        {
            ModelAnimationHeader* pAnim = (ModelAnimationHeader*)m_pAnimationSet->GetModelAnimationHeader(nAnimID0);
            if (pAnim)
            {
                nAnimID++;
                paramLoader.AddLoadedHeader(listID, *pAnim);        // store ModelAnimationHeader to reuse it in the case that a second model will need it
            }
        }
    }
#endif

    numAnims = animList.arrAnimFiles.size();
    for (int i = 0; i < numAnims; i++)
    {
        const char* pFilePath = animList.arrAnimFiles[i].m_FilePathQ;
        const char* pAnimName = animList.arrAnimFiles[i].m_AnimNameQ;
        const char* fileExt = PathUtil::GetExt(pFilePath);

        bool IsCHR = (0 == _stricmp(fileExt, CRY_SKEL_FILE_EXT));
        if (IsCHR)
        {
            continue;
        }
        bool IsSKIN = (0 == _stricmp(fileExt, CRY_SKIN_FILE_EXT));
        if (IsSKIN)
        {
            continue;
        }
        bool IsCDF = (0 == _stricmp(fileExt, "cdf"));
        if (IsCDF)
        {
            continue;
        }
        bool IsCGA = (0 == _stricmp(fileExt, "cga"));
        if (IsCGA)
        {
            continue;
        }

        int nAnimID0 = LoadAnimationFile(pFilePath, paramLoader, listID);

        if (nAnimID0 >= 0)
        {
            nAnimID++;
        }
    }
    return nAnimID;
}


uint32 CDefaultSkeleton::LoadAnimationFile(const char* animationPath, CParamLoader& paramLoader, uint32 listID)
{
    LOADING_TIME_PROFILE_SECTION(g_pISystem);
    
    const SAnimListInfo& animList = paramLoader.GetParsedList(listID);

    stack_string animPath = animationPath;
    stack_string animName = PathUtil::GetFileName(animPath);
    const char* pAnimName = animName.c_str();

    const char* fileExt = PathUtil::GetExt(animationPath);

    bool IsBAD = (0 == _stricmp(fileExt, ""));
    if (IsBAD)
    {
        return 0;
    }

    uint32 IsCAF = (_stricmp(fileExt, "caf") == 0);
    uint32 IsLMG = (_stricmp(fileExt, "lmg") == 0) || (_stricmp(fileExt, "bspace") == 0) || (_stricmp(fileExt, "comb") == 0);
    CRY_ASSERT(IsCAF + IsLMG);

    bool IsAimPose = 0;
    bool IsLookPose = 0;
    if (IsCAF)
    {
        uint32 numAimDB = m_poseBlenderAimDesc.m_blends.size();
        for (uint32 d = 0; d < numAimDB; d++)
        {
            const char* strAimIK_Token = m_poseBlenderAimDesc.m_blends[d].m_AnimToken;
            IsAimPose = (CryStringUtils::stristr(pAnimName, strAimIK_Token) != 0);
            if (IsAimPose)
            {
                break;
            }
        }
        uint32 numLookDB = m_poseBlenderLookDesc.m_blends.size();
        for (uint32 d = 0; d < numLookDB; d++)
        {
            const char* strLookIK_Token = m_poseBlenderLookDesc.m_blends[d].m_AnimToken;
            IsLookPose = (CryStringUtils::stristr(pAnimName, strLookIK_Token) != 0);
            if (IsLookPose)
            {
                break;
            }
        }
    }

    if (IsCAF && IsAimPose)
    {
        IsCAF = 0;
    }
    if (IsCAF && IsLookPose)
    {
        IsCAF = 0;
    }

    CryFixedStringT<256> standupAnimType;

    if (IsCAF)
    {
        m_pAnimationSet->LoadFileCAF(animationPath, pAnimName);
    }

    if (IsAimPose || IsLookPose)
    {
        m_pAnimationSet->LoadFileAIM(animationPath, pAnimName, this);
    }

    if (IsLMG)
    {
        m_pAnimationSet->LoadFileLMG(animationPath, pAnimName);
    }


    int nAnimID = m_pAnimationSet->GetAnimIDByName(pAnimName);
    if (nAnimID >= 0)
    {
        ModelAnimationHeader* pAnim = (ModelAnimationHeader*)m_pAnimationSet->GetModelAnimationHeader(nAnimID);
        if (pAnim)
        {
            // store ModelAnimationHeader to reuse it in the case that a second model will need it
            paramLoader.AddLoadedHeader(listID, *pAnim);
        }
    }
    else
    {
        gEnv->pLog->LogError("CryAnimation: Animation (caf) file \"%s\" could not be read (it's an animation of \"%s.%s\")", animationPath, animName.c_str(), CRY_SKEL_FILE_EXT);
    }
    
    return nAnimID;
}

const string CDefaultSkeleton::GetDefaultAnimDir()
{
    return "GameSDK";
}





