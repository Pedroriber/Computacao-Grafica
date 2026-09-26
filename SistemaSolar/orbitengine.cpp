#include "orbitengine.h"

OrbitEngine::OrbitEngine (TransformPtr pivot, float angularSpeed)
: m_pivot(pivot), m_angularSpeed(angularSpeed)
{
}

OrbitEnginePtr OrbitEngine::Make (TransformPtr pivot, float angularSpeed)
{
  return OrbitEnginePtr(new OrbitEngine(pivot, angularSpeed));
}

void OrbitEngine::Update (float dt)
{
  m_pivot->Rotate(m_angularSpeed * dt, 0.0f, 0.0f, -1.0f);
}
