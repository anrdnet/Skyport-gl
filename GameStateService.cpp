#include "GameStateService.h"
#include "entity/Billboard.h"

#define XOR(p1, p2) ((p1 || p2) && !(p1 && p2))

void GameStateService::Player::Update(const PlayerState &other)
{
    if(Name != other.Name)
        throw Error(Error::InvalidValue, "Updating name of player");

    StatsDirty |= other.Health != Health || other.Score != Score || 
        XOR(other.Index == 0, Index == 0);
    StateDirty |= other.Position != Position;
    Index = other.Index;
    if(StatsDirty)
    {
        Health = other.Health;
        Score = other.Score;
    }
    if(StateDirty)
    {
        Position = other.Position;
        VectorF2 pos(
                Hexmap::jOffset[X]*Position[X]+Hexmap::kOffset[X]*Position[Y],
                Hexmap::jOffset[Y]*Position[X]+Hexmap::kOffset[Y]*Position[Y]);
        PlayerMovable->Transform.Set(
                MatrixF4::Translation(VectorF4(pos[X],0.5,pos[Y])));
    }
    StatsDirty = false;
    StateDirty = false;
}

GameStateService::~GameStateService()
{
    for(auto pit = Players.begin(); 
            pit != Players.end(); pit++)
    {
        delete pit->PlayerMovable;
        delete pit->PlayerVisual;
    }
}

void GameStateService::SetCurrentPlayer()
{
    while(myCurrentPlayer->Index != 0)
    {
        myCurrentPlayer++;
        if(myCurrentPlayer == Players.end())
            myCurrentPlayer = Players.begin();
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
            mov->SetChild(bill);
            myContainer->AddChild(mov);
            bill->ProgramState().SetUniform("Z", -0.05f);
            Players.push_back(Player(i++,pit->Name,mov,bill));
            Players.back().Update(*pit);
        }
        myMap->Create(mapSize[X],mapSize[Y]);
        myCurrentPlayer = Players.begin();
    }
    else
    {
        int i = 0;
        for(auto pit = state.Players_begin(); 
                pit != state.Players_end(); pit++)
        {
            Players[i++].Update(*pit);
        }
    }
    for(int j = 0;  j < mapSize[X]; j++)
    {
        for(int k = 0;  k < mapSize[Y]; k++)
        {
            myMap->SetTileType(j,k,state.GetMap()(j,k));
        }
    }
    SetCurrentPlayer();

    if(Turn != state.GetTurn())
    {
        myActionCursor = 0;
    }

    myActionCount = state.GetActionCount();
    for(uint i = 0; i < myActionCount; i++)
    {
        myActionStates[i] = state.GetAction(i);
    }

    PlayAnimation();

    Turn = state.GetTurn();
}

VectorF2 DirectionToOffset(Direction dir)
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
        case Direction::Left_up:
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
    return Hexmap::jOffset * tileoff[X] + Hexmap::kOffset * tileoff[Y];
} 

void GameStateService::PlayAnimation()
{
    if(myAnimations.GetAnimationCount() == 0)
    {
        if(myActionCursor < myActionCount)
        {
            switch(myActionStates[myActionCursor].GetAction())
            {
                case SkyportAction::Move:
                    {
                        VectorF4 pos;
                        myCurrentPlayer->PlayerMovable->Transform.Get()
                            .GetTranslation(pos);

                        VectorF2 off = DirectionToOffset(
                                myActionStates[myActionCursor].GetDirection());
                        AnimationHelper::AnimationData data(
                                myCurrentPlayer->PlayerMovable,
                                pos, VectorF4(
                                    pos[X] + off[X], pos[Y], pos[Z]+off[Y]), 
                                1, AnimationHelper::SmoothCurve);
                        Debug("Added move animation");
                        myAnimations.AddAnimation(data);
                    }
                    break;
                default:
                    Debug("Action not implemented, may hang.");
            }
            myActionCursor++;
        }
        else
        {
            Event nevent(SkyportEventClass::GameState, 
                    GameStateEventCodes::StateProcessed, this);
            myDonePin.Send(nevent);
        }
    }
}

void GameStateService::AnimationDone()
{
    PlayAnimation();
}

bool GameStateService::StateUpdate(Event &event, InPin pin)
{
    GameStateEvent &gevent = dynamic_cast<GameStateEvent&>(event);
    Update(gevent.GetState());
    Debug("Updated gamesate");
    return true;
}
