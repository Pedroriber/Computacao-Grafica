#include "luxorengine.h"
#include "linearinterpolator.h"
#include "cubicinterpolator.h"

LuxorEngine::LuxorEngine (
               TransformPtr trf_all,
               TransformPtr trf_base,
               TransformPtr trf_haste1,
               TransformPtr trf_haste2,
               TransformPtr trf_haste3,
               TransformPtr trf_cupula,
               TransformPtr trf_lampada
              )
: m_reverse(false),
  m_head_angle(0.0f),
  m_status("up"),
  m_curr_anim(),
  m_trf_all(trf_all),
  m_trf_base(trf_base),
  m_trf_haste1(trf_haste1),
  m_trf_haste2(trf_haste2),
  m_trf_haste3(trf_haste3),
  m_trf_cupula(trf_cupula),
  m_trf_lampada(trf_lampada)
{
  CreateStandDownAnimation();
  CreateJumpForwardAnimation();
}

LuxorEnginePtr LuxorEngine::Make (
               TransformPtr trf_all,
               TransformPtr trf_base,
               TransformPtr trf_haste1,
               TransformPtr trf_haste2,
               TransformPtr trf_haste3,
               TransformPtr trf_cupula,
               TransformPtr trf_lampada
              )
{
  return LuxorEnginePtr(new LuxorEngine(trf_all,
                                        trf_base,
                                        trf_haste1,
                                        trf_haste2,
                                        trf_haste3,
                                        trf_cupula,
                                        trf_lampada
                                       )
                        );
}

LuxorEngine::~LuxorEngine ()
{
}

void LuxorEngine::CreateStandDownAnimation ()
{
  MovementPtr move = Movement::Make(0.5f);
  move->AddRotation(m_trf_haste1,
                    LinearInterpolator::Make(Vec3{0.0f,0.0f,0.0f},
                                             Vec3{-30.0f,0.0f,0.0f}
                                            )
                   );
  move->AddRotation(m_trf_haste2,
                    LinearInterpolator::Make(Vec3{0.0f,0.0f,0.0f},
                                             Vec3{120.0f,0.0f,0.0f}
                                            )
                   );
  move->AddRotation(m_trf_haste3,
                    LinearInterpolator::Make(Vec3{0.0f,0.0f,0.0f},
                                             Vec3{-120.0f,0.0f,0.0f}
                                            )
                   );
  move->AddRotation(m_trf_cupula,
                    LinearInterpolator::Make(Vec3{0.0f,0.0f,0.0f},
                                             Vec3{30.0f,0.0f,0.0f}
                                            )
                   );
  m_stand_down_anim = Animation::Make({move});
}

// Salto no estilo do curta "Luxo Jr." (Pixar). Os quadros-chave da tabela
// de poses (abaixada, agachada, no alto, pouso, assentada) foram mantidos,
// e os principios classicos de animacao foram acrescentados em volta deles:
//
//   antecipacao   sobe um pouco antes de agachar (movimento contrario) e
//                 agacha mais fundo que a pose "agachada" da tabela
//   impulso       extensao rapida das hastes ainda no chao (esticar)
//   voo           trajetoria balistica (tangentes de Hermite: sai rapido,
//                 desacelera ate o topo, desce acelerando), poses da tabela
//   pouso         comprime (amassar) mais que a pose de pouso da tabela
//   acomodacao    rebote que passa da pose final (overshoot) e assenta
//
// As juntas usam interpolacao cubica com tangentes nulas (entra e sai
// suave) onde o movimento deve ser macio, e linear no impulso (explosivo).
// Cada movimento comeca onde o anterior termina; como o motor aplica
// diferencas, a animacao tambem funciona ao contrario (pular para tras).
void LuxorEngine::CreateJumpForwardAnimation ()
{
  struct Pose { float base, h1, h2, h3, cup; };
  const Pose down    = {  0.0f, -30.0f, 120.0f, -120.0f, 30.0f};  // assentada (tabela)
  const Pose lift    = {  0.0f, -24.0f, 110.0f, -112.0f, 24.0f};  // antecipacao: sobe um pouco
  const Pose crouch  = {  0.0f, -42.0f, 152.0f, -148.0f, 62.0f};  // agachada (tabela: -40/150/-145/60), mais funda
  const Pose push    = {  0.0f, -12.0f,  80.0f,  -85.0f, 48.0f};  // impulso: estica no chao
  const Pose apex    = {-30.0f,  10.0f,  50.0f,  -50.0f, 65.0f};  // no alto (tabela)
  const Pose land    = {  0.0f, -60.0f, 160.0f, -165.0f, 60.0f};  // pouso (tabela)
  const Pose squash  = {  0.0f, -68.0f, 170.0f, -174.0f, 72.0f};  // amassa ao pousar
  const Pose rebound = {  0.0f, -22.0f, 108.0f, -110.0f, 20.0f};  // rebote (passa da pose final)

  auto joint = [](MovementPtr mv, TransformPtr trf, float a, float b, bool ease) {
    Vec3 p0{a, 0.0f, 0.0f}, p1{b, 0.0f, 0.0f};
    if (ease)
      mv->AddRotation(trf, CubicInterpolator::Make(p0, Vec3{0.0f, 0.0f, 0.0f}, p1, Vec3{0.0f, 0.0f, 0.0f}));
    else
      mv->AddRotation(trf, LinearInterpolator::Make(p0, p1));
  };
  auto pose_move = [&](float T, const Pose& a, const Pose& b, bool ease) {
    MovementPtr mv = Movement::Make(T);
    joint(mv, m_trf_base,   a.base, b.base, ease);
    joint(mv, m_trf_haste1, a.h1,   b.h1,   ease);
    joint(mv, m_trf_haste2, a.h2,   b.h2,   ease);
    joint(mv, m_trf_haste3, a.h3,   b.h3,   ease);
    joint(mv, m_trf_cupula, a.cup,  b.cup,  ease);
    return mv;
  };

  MovementPtr m1 = pose_move(0.12f, down,    lift,    true);    // antecipacao: sobe
  MovementPtr m2 = pose_move(0.32f, lift,    crouch,  true);    // agacha fundo
  MovementPtr m3 = pose_move(0.10f, crouch,  push,    false);   // impulso no chao
  // voo, subida: sai rapido do chao e desacelera ate o topo (y: 0 -> 30)
  MovementPtr m4 = pose_move(0.45f, push,    apex,    true);
  m4->AddTranslation(m_trf_all,
                     CubicInterpolator::Make(Vec3{0.0f, 0.0f, 0.0f},  Vec3{0.0f, 60.0f, 45.0f},
                                             Vec3{0.0f, 30.0f, 50.0f}, Vec3{0.0f, 0.0f, 45.0f}));
  // voo, descida: acelera ate o chao (y: 30 -> 0), z continua em ritmo constante
  MovementPtr m5 = pose_move(0.45f, apex,    land,    true);
  m5->AddTranslation(m_trf_all,
                     CubicInterpolator::Make(Vec3{0.0f, 30.0f, 50.0f}, Vec3{0.0f, 0.0f, 45.0f},
                                             Vec3{0.0f, 0.0f, 90.0f},  Vec3{0.0f, -60.0f, 45.0f}));
  MovementPtr m6 = pose_move(0.10f, land,    squash,  true);    // amassa
  MovementPtr m7 = pose_move(0.16f, squash,  rebound, true);    // rebote (overshoot)
  MovementPtr m8 = pose_move(0.26f, rebound, down,    true);    // assenta
  m_jump_forward_anim = Animation::Make({m1, m2, m3, m4, m5, m6, m7, m8});
}

bool LuxorEngine::StandUp ()
{
  if (m_curr_anim || m_status != "down")
    return false;
  m_curr_anim = m_stand_down_anim;
  m_reverse = true;
  m_status = "up";   // next status
  return true;
}

bool LuxorEngine::StandDown ()
{
  if (m_curr_anim || m_status != "up")
    return false;
  m_curr_anim = m_stand_down_anim;
  m_reverse = false;
  m_status = "down";     // next status
  return true;
}

bool LuxorEngine::JumpForward ()
{
  if (m_curr_anim || m_status != "down")
    return false;
  m_curr_anim = m_jump_forward_anim;
  m_reverse = false;
  m_status = "down";   // next status
  return true;
}

bool LuxorEngine::JumpBackward ()
{
  if (m_curr_anim || m_status != "down")
    return false;
  m_curr_anim = m_jump_forward_anim;
  m_reverse = true;
  m_status = "down";   // next status
  return true;
}

void LuxorEngine::TurnHead (float angle)
{
  m_trf_cupula->Rotate(angle,0.0f,1.0f,0.0f);
  m_head_angle += angle;
}

void LuxorEngine::Update (float dt)
{
  if (m_curr_anim) {
    if (m_curr_anim->Advance(dt,m_reverse)) {
      m_curr_anim = nullptr;
    }
  }
}
