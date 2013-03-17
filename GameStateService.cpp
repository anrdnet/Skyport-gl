#include "GameStateService.h"
#include "entity/Billboard.h"
#include "MortarAnimation.h"

#define XOR(p1, p2) ((p1 || p2) && !(p1 && p2))

void GameStateService::Player::Update(const PlayerState &other)
{
    if(Name != other.Name)
        throw Error(Error::InvalidValue, "Updating name of player");

    //StatsDirty |= other.Health != Health || other.Score != Score || 
    //    XOR(other.Index == 0, Index == 0);
    //StateDirty |= other.Position != Position;
    Index = other.Index;

    Score = other.Score;
    if(IsDead && Health != 0)
    {
        IsDead = false;
        PlayerVisual->Visible.Set(true);
        PlayerNametag->Visible.Set(true);
        Spawned = true;
        Debug("Player spawning");
    }
    if(other.Health != Health)
    {
        Health = other.Health;
        PlayerNametag->Health.Set(Health/100.0f);
        if(Health == 0)
        {
            Died = true;
            IsDead = true;
            PlayerVisual->Visible.Set(false);
            PlayerNametag->Visible.Set(false);
        }
    }
    if(Position != other.Position)
    {
        Debug("Warning: Jumping on gamesate");
        Position = other.Position;
        VectorF2 pos(
                Hexmap::jOffset[X]*Position[X]+Hexmap::kOffset[X]*Position[Y],
                Hexmap::jOffset[Y]*Position[X]+Hexmap::kOffset[Y]*Position[Y]);
        PlayerMovable->Transform.Set(
                MatrixF4::Translation(VectorF4(pos[X],0.0,pos[Y])));
    }
}

GameStateService::GameStateService(MultiContainer *container, Hexmap *map,
        AssetRef<Texture> figureTexture, AssetRef<Texture> laserTexture,
        AssetRef<Texture> mortarTexture, AssetRef<Texture> droidTexture, 
        AssetRef<Texture> explosionTexture,
        Camera *camera) 
    : myAnimations(this), Turn(-1), myContainer(container), myMap(map), 
    myFigureTexture(figureTexture), myActionCount(0), myActionCursor(0),
    myCamera(camera), myLaser(laserTexture), myMortar(mortarTexture),
    myInMortar(false), myDroid(droidTexture), myDroidSequenceCounter(-1),
    myDoExplode(false), myExplosion(explosionTexture)
{
    RegisterInPin(SkyportEventClass::GameState, "StateUpdates", 
            static_cast<EventCallback>(&GameStateService::StateUpdate));
    myDonePin = RegisterOutPin(SkyportEventClass::GameState, "Done");
    mySoundPin = RegisterOutPin(SkyportEventClass::Sound, "Sound");
    myTitle.Color.Set(ColorRGBA(255,255,0,255));
    myTitle.Text.Set("Welcome");
    myTitle.Anchors.Set(Anchor::Top);
    myTitle.Size.Set(SizeF2(0.1,0.2));
    myTitle.Position.Set(VectorF2(0,0.1));

    mySubtitle.Color.Set(ColorRGBA(255,255,0,255));
    mySubtitle.Text.Set("Game will begin shortly");
    mySubtitle.Anchors.Set(Anchor::Bottom);
    mySubtitle.Size.Set(SizeF2(0.1,0.15));

    container->AddChild(&myTitle);
    container->AddChild(&mySubtitle);

    for(int i = 0; i < 6; i++)
    {
        myBiexplosions[i].SetTexture(explosionTexture);
    }
}

GameStateService::~GameStateService()
{
    for(auto pit = Players.begin(); 
            pit != Players.end(); pit++)
    {
        delete pit->PlayerMovable;
        delete pit->PlayerVisual;
        delete pit->NametagMovable;
        delete pit->PlayerContainer;
        delete pit->PlayerNametag;
    }
}


VectorI2 DirectionToTileOffset(Direction dir)
{
    VectorI2 tileoff;
    switch(dir)
    {
        case Direction::None:
            break;
        case Direction::Up:
            tileoff = VectorI2(-1,-1);
            break;
        case Direction::Down:
            tileoff = VectorI2(1 , 1);
            break;
        case Direction::Left_Up:
            tileoff = VectorI2(0,-1);
            break;
        case Direction::Left_Down:
            tileoff = VectorI2(1,0);
            break;
        case Direction::Right_Up:
            tileoff = VectorI2(-1,0);
            break;
        case Direction::Right_Down:
        tileoff = VectorI2(0,1);
            break;
    }
    return tileoff;
} 

VectorF2 TileToPosition(VectorI2 tile)
{
    return Hexmap::jOffset * tile[X] + Hexmap::kOffset * tile[Y];
}

VectorF2 DirectionToOffset(Direction dir)
{
    return TileToPosition(DirectionToTileOffset(dir));
}
void GameStateService::SetCurrentPlayer()
{
    while(myCurrentPlayer->Index != 0)
    {
        myCurrentPlayer++;
        if(myCurrentPlayer == Players.end())
            myCurrentPlayer = Players.begin();
    }
    if(!myCurrentPlayer->IsDead)
        myCurrentPlayer->PlayerMovable->Transform.Get().GetTranslation(myCameraTarget);
}

void GameStateService::MoveCamera(real time, real dragTime)
{
    if(myAnimations.GetNonPermanentCount() == 0)
    {
        ForceMoveCamera(time, dragTime);
    }
    else
    {
        Debug("Not moving camera, %d running", myAnimations.GetNonPermanentCount());
    }
}
void GameStateService::ForceMoveCamera(real time, real dragTime)
{
    VectorF4 oldtarget;
    myCamMarkerMov.Transform.Get().GetTranslation(oldtarget);

    if((oldtarget - myCameraTarget).SquareLength() > 0.1)
    {
        AnimationHelper::TranslationAnimationData *markdata =
            new AnimationHelper::TranslationAnimationData(
                    &myCamMarkerMov,
                    oldtarget, 
                    myCameraTarget,
                    time, AnimationHelper::SmoothCurve);
        myAnimations.AddAnimation(markdata);

        AnimationHelper::TranslationAnimationData *camdata =
            new AnimationHelper::TranslationAnimationData(
                    &myCamMov,
                    oldtarget + VectorF4(0, 10, 10), 
                    myCameraTarget + VectorF4(0, 10, 10),
                    time+dragTime, AnimationHelper::SmoothCurve);
        myAnimations.AddAnimation(camdata);
        Debug("Start movement");
    }
}

void GameStateService::Update(const GameState &state)
{
    VectorI2 mapSize = state.GetMap().GetSize();
    if(Turn == -1)
    {
        int i = 0;
        for(auto pit = state.Players_begin(); 
                pit != state.Players_end(); pit++)
        {
            Movable *mov = new Movable();
            Billboard *bill = new Billboard(myFigureTexture);
            Movable *nameMov = new Movable();
            MultiContainer *container = new MultiContainer();
            Nametag *nametag = new Nametag();
            bill->Offset.Set(VectorF2(0,0.5));
            nametag->Offset.Set(VectorF2(0,1.15));

            nametag->PlayerName.Set(pit->Name);
            nametag->Health.Set(pit->Health/100.0f);
            nametag->Visible.Set(false);

            mov->SetChild(container);
            container->AddChild(bill);
            container->AddChild(nameMov);
            nameMov->SetChild(nametag);
            myContainer->AddChild(mov);
            bill->ProgramState().SetUniform("Z", -0.05f);
            bill->ProgramState().SetUniform("FrameCount", VectorI2(16,2));
            bill->Visible.Set(false);
            nametag->ProgramState().SetUniform("Size", VectorF2(1.6,0.2));
            Players.push_back(Player(i++,pit->Name,mov,bill,nameMov,
                        container, nametag));
            Players.back().Update(*pit);
        }
        myMap->Create(mapSize[X],mapSize[Y]);
        myCurrentPlayer = Players.begin();

        AnimationHelper::HideAnimationData *hanim = 
                new AnimationHelper::HideAnimationData(&mySubtitle, 6);
        hanim->Presistant = true;
        mySubtitleAnimation = myAnimations.AddAnimation(hanim);

        VectorF2 midpoint = TileToPosition(mapSize / 2);
        myDefaultLookat = VectorF4(midpoint[X], 0.0f, midpoint[Y]);
        myDefaultCamera = VectorF4(0,10,10) + myDefaultLookat; //VectorF4(-midpoint[X], abs(midpoint[X] + midpoint[Y]), -midpoint[Y]);
        myCameraTarget = myDefaultLookat;

        myCamera->Near.Set(0.5);
        myCamera->Far.Set(200);
        myCamera->FOV.Set(3.14/4);

        myCamMov.Transform.Set(MatrixF4::Translation(myDefaultCamera));
        myCamMov.SetPointAt(&myCamMarker);
        myCamMov.InstanceId.Set(1);
        myCamMov.SetChild(myCamera);

        myCamMarkerMov.InstanceId.Set(2);
        myCamMarkerMov.Transform.Set(MatrixF4::Translation(myDefaultLookat));
        myCamMarkerMov.SetChild(&myCamMarkerC);

        myCamMarkerViz.Color.Set(ColorF(1,0,1,1));
        myCamMarkerViz.Size.Set(0.05f);

        myCamMarkerC.AddChild(&myCamMarkerViz);
        myCamMarkerC.AddChild(&myCamMarker);

        myContainer->AddChild(&myCamMarkerMov);
        myContainer->AddChild(&myCamMov);

        real laserScale = Hexmap::jOffset.Length();
        myLaserBaseTransform = MatrixF4::RotationX(-Pi / 2) 
            * MatrixF4::Scale(VectorF4(laserScale, laserScale, laserScale, 1));
        myLaser.Visible.Set(false);
        myLaserMov.SetChild(&myLaser);
        myLaserMov.Transform.Set(myLaserBaseTransform);
        myContainer->AddChild(&myLaserMov);
        myLaser.Length.Set(8);

        myMortarMov.SetChild(&myMortar);
        myContainer->AddChild(&myMortarMov);
        myMortar.ProgramState().SetUniform("FrameCount", VectorI2(1,16));
        myMortar.ProgramState().SetUniform("Size", VectorF2(0.5,0.5));
        myMortar.Visible.Set(false);

        myContainer->AddChild(&myExplosionMov);
        myExplosionC.AddChild(&myExplosion);
        myExplosionMov.SetChild(&myExplosionC);
        myExplosion.ProgramState().SetUniform("FrameCount", VectorI2(16,1));
        myExplosion.ProgramState().SetUniform("Offset", VectorF2(0,0.5));
        myExplosion.ProgramState().SetUniform("Size", VectorF2(2,2));
        myExplosion.Visible.Set(false);

        for(int i = 0; i < 6; i++)
        {
            Direction dir;
            switch(i)
            {
                case 0:
                    dir = Direction::Up;
                    break;
                case 1:
                    dir = Direction::Down;
                    break;
                case 2:
                    dir = Direction::Left_Up;
                    break;
                case 3:
                    dir = Direction::Left_Down;
                    break;
                case 4:
                    dir = Direction::Right_Up;
                    break;
                case 5:
                    dir = Direction::Right_Down;
                    break;

            }
            VectorF2 offset = DirectionToOffset(dir);

            myExplosionC.AddChild(myBiexplosionMoves + i);
            myBiexplosionMoves[i].SetChild(myBiexplosions + i);
            myBiexplosionMoves[i].Transform
                .Set(MatrixF4::Translation(VectorF4(offset[X], 0, offset[Y])));
            myBiexplosions[i].ProgramState().SetUniform("FrameCount", VectorI2(16,1));
            myBiexplosions[i].ProgramState().SetUniform("Offset", VectorF2(0,0.5));
            myBiexplosions[i].ProgramState().SetUniform("Size", VectorF2(1,1));
            myBiexplosions[i].Visible.Set(false);
        }

        myDroidMov.SetChild(&myDroid);
        myContainer->AddChild(&myDroidMov);
        myDroid.Visible.Set(false);
        myDroid.ProgramState().SetUniform("Offset", VectorF2(0,0.5));

        for(uint i = 0; i < MeteorCount; i++)
        {
            myMeteorMovs[i].SetChild(&myMeteors[i]);
            myMeteorMovs[i].Transform.Set(MatrixF4::RotationZ(-0.463647609));
            MeteorAnimationData *adata = 
                new MeteorAnimationData(&myMeteorMovs[i], 10, 
                        AnimationHelper::LinearCurve);
            adata->CurrentTime = double(std::rand()) * 10 / double(RAND_MAX);
            adata->Finalize();
            myAnimations.AddAnimation(adata);

            AnimationHelper::TextureAnimationData *tedata =
                new AnimationHelper::TextureAnimationData(
                    &myMeteors[i], 16, X, 1,
                    AnimationHelper::LinearCurve);
            tedata->Repeating = true;
            myAnimations.AddAnimation(tedata);

            myContainer->AddChild(myMeteorMovs + i);
        }

        //myStats = new Statusbox();
        //myStats->State.Set(state);
        //myContainer->AddChild(myStats);
    }
    else
    {
        int i = 0;
        for(auto pit = state.Players_begin(); 
                pit != state.Players_end(); pit++)
        {
            Players[i].Update(*pit);
            if(Players[i].GetDied())
            {
                myDyingPlayers.push_back(i);
            }
            i++;
        }
    }

    for(int j = 0;  j < mapSize[X]; j++)
    {
        for(int k = 0;  k < mapSize[Y]; k++)
        {
            myMap->SetTileType(j,k,state.GetMap()(j,k));
        }
    }
    if(myDyingPlayers.size() == 0)
    {
        Debug("Last turn: %d, new turn: %d", Turn, state.GetTurn());
        SetCurrentPlayer();
        MoveCamera();
    }
    else
    {
        Debug("Someone died!");
    }

    if(Turn != state.GetTurn())
    {
        myActionCursor = 0;
    }

    myActionCount = state.GetActionCount();
    for(uint i = 0; i < myActionCount; i++)
    {
        myActionStates[i] = state.GetAction(i);
    }

    myTitle.Text.Set(state.GetTitle());
    if(state.GetSubtitle() != mySubtitle.Text.Get())
    {
        mySubtitle.Text.Set(state.GetSubtitle());
        myAnimations.ResetAnimation(mySubtitleAnimation);
    }

    PlayAnimation();

    Turn = state.GetTurn();
}

real DirectionToAngle(Direction dir)
{
    switch(dir)
    {
        case Direction::Up:
            return 3 * Pi / 2;
        case Direction::Down:
            return Pi / 2;
        case Direction::Left_Down:
            return Pi / 6;
        case Direction::Right_Down:
            return 5 * Pi / 6;
        case Direction::Right_Up:
            return 7 * Pi / 6;
        case Direction::Left_Up:
            return 11 * Pi / 6;
        default:
            Warning("Unknown laser direction.");
            return 0;
    }
}

void GameStateService::Explode(VectorF4 pos)
{
    AnimationHelper::HideAnimationData *hdata = 
        new AnimationHelper::HideAnimationData(&myExplosion, 1);

    AnimationHelper::TextureAnimationData *tdata = 
        new AnimationHelper::TextureAnimationData(
                &myExplosion, 16, X, 1, AnimationHelper::LinearCurve);


    myExplosionMov.Transform.Set(MatrixF4::Translation(pos));
    myExplosion.Visible.Set(true);
    myAnimations.AddAnimation(hdata);
    myAnimations.AddAnimation(tdata);

    for(int i = 0; i < 6; i++)
    {
        myBiexplosions[i].Visible.Set(true);
        AnimationHelper::HideAnimationData *hbdata = 
            new AnimationHelper::HideAnimationData(myBiexplosions + i, 1);

        AnimationHelper::TextureAnimationData *tbdata = 
            new AnimationHelper::TextureAnimationData(
                    myBiexplosions + i, 16, X, 1, AnimationHelper::LinearCurve);
        myAnimations.AddAnimation(hbdata);
        myAnimations.AddAnimation(tbdata);
    }
}

void GameStateService::PlayAnimation()
{
    if(myAnimations.GetNonPermanentCount() == 0)
    {
        if(myDyingPlayers.size() != 0)
        {
            VectorF4 pos;
            Player &dyingplayer = Players[myDyingPlayers.back()];
            myDyingPlayers.pop_back();
            dyingplayer.PlayerMovable->Transform.Get()
                .GetTranslation(pos);
            VectorF4 camoff = pos - myCameraTarget;
            if(camoff.SquareLength() > 1)
            {
                myCameraTarget = pos;
                ForceMoveCamera();
            }
            else
            {
                dyingplayer.PlayerVisual->ProgramState().SetUniform("Frame", 
                        VectorI2(0,1));
                AnimationHelper::TextureAnimationData *tedata =
                    new AnimationHelper::TextureAnimationData(
                        dyingplayer.PlayerVisual, 16, X, 1,
                        AnimationHelper::LinearCurve);
                myAnimations.AddAnimation(tedata);
            }
        }
        else if(myAnimatingDying)
        {
            SetCurrentPlayer();
            MoveCamera();
            myAnimatingDying = false;
        }
        else if(myCurrentPlayer->GetSpawned())
        {
            VectorF4 pos;
            myCurrentPlayer->PlayerMovable->Transform.Get()
                .GetTranslation(pos);
            Explode(pos);
        }
        else if(myInMortar)
        {
            myMortar.Visible.Set(false);
            VectorF4 pos;
            myMortarMov.Transform.Get().GetTranslation(pos);
            if(myDoExplode)
            {
                Explode(pos);
                PlaySound(Sound::MotarImpact);
            }

            myInMortar = false;
            myActionCursor++;
        }
        else if(myDroidSequenceCounter != -1)
        {
            while(myDroidSequenceCounter < ActionState::MaxDroidCommands)
            {
                Direction dir = myActionStates[myActionCursor]
                    .GetCommands()[myDroidSequenceCounter];
                if(dir != Direction::None)
                {
                    VectorF4 pos;
                    myDroidMov.Transform.Get().GetTranslation(pos);

                    VectorF2 off = DirectionToOffset(dir);
                    AnimationHelper::TranslationAnimationData *trdata =
                        new AnimationHelper::TranslationAnimationData(
                            &myDroidMov, pos, 
                            VectorF4(pos[X] + off[X], pos[Y], pos[Z]+off[Y]), 
                            1, AnimationHelper::SmoothCurve);
                    myAnimations.AddAnimation(trdata);

                    myCameraTarget = VectorF4(pos[X] + off[X], pos[Y], pos[Z]+off[Y]);
                    ForceMoveCamera();
                    myDroidSequenceCounter++;
                    PlaySound(Sound::DroidStep);
                    return;
                }
                myDroidSequenceCounter++;
            }
            myActionCursor++;
            myDroidSequenceCounter = -1;

            myDroid.Visible.Set(false);

            VectorF4 pos;
            myDroidMov.Transform.Get().GetTranslation(pos);
            Explode(pos);

            PlaySound(Sound::DroidFire);
        }
        else if(myActionCursor < myActionCount)
        {
            if(myCurrentPlayer->Index != 0)
                throw Error(Error::InvalidState, "Action on wrong player");
            Debug("Do action");
            switch(myActionStates[myActionCursor].GetAction())
            {
                case SkyportAction::Move:
                    {
                        VectorF4 pos;
                        myCurrentPlayer->PlayerMovable->Transform.Get()
                            .GetTranslation(pos);

                        VectorI2 tileoff = DirectionToTileOffset(
                                myActionStates[myActionCursor].GetDirection());
                        VectorF2 off = TileToPosition(tileoff);
                        myCurrentPlayer->Position += tileoff;
                        AnimationHelper::TranslationAnimationData *trdata =
                            new AnimationHelper::TranslationAnimationData(
                                myCurrentPlayer->PlayerMovable,
                                pos, 
                                VectorF4(pos[X] + off[X], pos[Y], pos[Z]+off[Y]), 
                                1, AnimationHelper::SmoothCurve);
                        myAnimations.AddAnimation(trdata);

                        myCameraTarget = VectorF4(pos[X] + off[X], pos[Y], pos[Z]+off[Y]);
                        ForceMoveCamera();

                        //AnimationHelper::TextureAnimationData *tedata =
                        //    new AnimationHelper::TextureAnimationData(
                        //        myCurrentPlayer->PlayerVisual, 4, X, 1,
                        //        AnimationHelper::LinearCurve);
                        //myAnimations.AddAnimation(tedata);
                        myActionCursor++;
                    }
                    break;
                case SkyportAction::Laser:
                    {
                        VectorF4 pos;
                        myCurrentPlayer->PlayerMovable->Transform.Get()
                            .GetTranslation(pos);
                        pos[Y] = 0.5;
                        MatrixF4 localtransform = MatrixF4::RotationY(
                                DirectionToAngle(myActionStates[myActionCursor].
                                    GetDirection()));
                        localtransform.SetTranslation(pos);
                        myLaserMov.Transform.Set(localtransform 
                                * myLaserBaseTransform);

                        VectorI2 offset = myActionStates[myActionCursor].GetCoordinate();
                        Debug("Laser offset: "+static_cast<std::string>(offset));
                        int length = std::max(abs(offset[X]), abs(offset[Y]));
                        VectorF2 off = TileToPosition(offset)/2;


                        myLaser.Length.Set(0);
                        myLaser.Visible.Set(true);
                        real rollSpeed = 0.3/8;
                        LaserAnimationData *ldata = 
                            new LaserAnimationData(&myLaser, length+0.5, 0.3/8, 
                                    AnimationHelper::LinearCurve);
                        myAnimations.AddAnimation(ldata);

                        AnimationHelper::TextureAnimationData *tedata =
                            new AnimationHelper::TextureAnimationData(
                                &myLaser, 16, Y, 1,
                                AnimationHelper::LinearCurve);
                        myAnimations.AddAnimation(tedata);

                        AnimationHelper::HideAnimationData *hdata = 
                            new AnimationHelper::HideAnimationData(&myLaser, 1);
                        myAnimations.AddAnimation(hdata);
                        myCameraTarget = VectorF4(pos[X] + off[X], pos[Y], pos[Z]+off[Y]);
                        ForceMoveCamera(std::min(rollSpeed * (length + 0.5) + 0.2, 1.0), 0.1);

                        PlaySound(Sound::Laser, 1);

                        myActionCursor++;
                    }
                    break;
                case SkyportAction::Motar:
                    {
                        VectorF4 pos;
                        myCurrentPlayer->PlayerMovable->Transform.Get()
                            .GetTranslation(pos);
                        pos[Y] = 0;

                        VectorF2 target = TileToPosition(
                                myActionStates[myActionCursor].GetCoordinate()) 
                            + VectorF2(pos[X], pos[Z]);

                        myMortar.Visible.Set(true);

                        MortarAnimationData *mdata = 
                            new MortarAnimationData(&myMortarMov, 1.5, pos, 
                                    VectorF4(target[X], 0, target[Y]), 5,
                                    AnimationHelper::LinearCurve);
                        myAnimations.AddAnimation(mdata);
                        PlaySound(Sound::MotarFire);
                        PlaySound(Sound::MotarAir, 5);

                        VectorI2 targetTile = myCurrentPlayer->Position +
                            myActionStates[myActionCursor].GetCoordinate();
                        char t = myMap->GetTileType(targetTile[X],targetTile[Y]);
                        Debug(std::string("Type is: ")+t);
                        myDoExplode = !(t == 'V' || t == 'O');
                        
                        myInMortar = true;
                    }
                    break;
                case SkyportAction::Droid:
                    {
                        PlaySound(Sound::DroidFire);
                        myDroid.Visible.Set(true);

                        VectorF4 pos;
                        myCurrentPlayer->PlayerMovable->Transform.Get()
                            .GetTranslation(pos);
                        pos[Y] = 0;
                        myDroidMov.Transform.Set(MatrixF4::Translation(pos));
                        myDroidSequenceCounter = 0;
                    }
                    break;
                default:
                    Debug("Action not implemented, may hang.");
                    myActionCursor++;
            }
        }
        else
        {
            Event nevent(SkyportEventClass::GameState, 
                    GameStateEventCodes::StateProcessed, this);
            myDonePin.Send(nevent);
        }
    }
}

void GameStateService::PlaySound(Sound sound, real duration)
{
    SoundEvent sevent(SoundEventCodes::Play, this, sound, duration);
    mySoundPin.Send(sevent);
}

void GameStateService::AnimationDone()
{
    PlayAnimation();
}

bool GameStateService::StateUpdate(Event &event, InPin pin)
{
    GameStateEvent &gevent = dynamic_cast<GameStateEvent&>(event);
    Update(gevent.GetState());
    return true;
}
