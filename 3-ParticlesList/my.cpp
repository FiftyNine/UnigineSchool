#include <vector>

using namespace std;

// Enable allocation of new particles from pool
#define __POOL_ALLOCATOR__

/************************************* Declaration *******************************************/

// Particle type intended for an intrusive linked list
struct Particle : public IParticle
{
	Particle * next{ nullptr };
	Particle * prev{ nullptr };
};

// Reserved pool of memory for faster creation and deletion of particles
class ParticleAllocator
{
public:
	// Set single pool size
	explicit ParticleAllocator(int pool_size);
	// Set pool size by system setting
	explicit ParticleAllocator(const Settings & s);
	~ParticleAllocator();

	Particle * Create();
	void Kill(Particle * p);
private:	
	vector<Particle *> pools;
	// Available spots in pools for creation of new particles. Wastes memory, but hopefully improves preformance
	vector<Particle *> free_spots;
	int pool_size;

	// Estimate a number of concurrent particles needed for a system
	int Estimate(const Settings & s);
	// Allocate new pool and add it to the list
	void AddPool();
};

// Particle system implementing an intrusive linked list
class System : public ISystem<IParticle *>
{
public:
	explicit System(vec3 system_pos, const Settings & s);
protected:
	IParticle * Create() override;
	IParticle * Kill(IParticle * p) override;
	IParticle * GetFirst() override;
	IParticle * GetEnd() override;
	IParticle * GetNext(IParticle * p) override;
private:
	Particle * first;
	// Pool allocator for faster list manipulation
	ParticleAllocator allocator;

	Particle * CreateInternal();
	void KillInternal(Particle * p);
};


// Particle system based on std::list implementation
typedef list<IParticle>::iterator ListIt;

class StdSystem : public ISystem<ListIt>
{
public:
	explicit StdSystem(vec3 system_pos, const Settings & s);
protected:
	ListIt Create() override;
	ListIt Kill(ListIt p) override;
	ListIt GetFirst() override;
	ListIt GetEnd() override;
	ListIt GetNext(ListIt p) override;
private:
	std::list<IParticle> list;
};

/************************************* Definition *******************************************/

#pragma region "ParticleAllocator: Reserved pool of memory for faster creation and deletion of particles"

ParticleAllocator::ParticleAllocator(int pool_size) 
	: pool_size(pool_size > 0 ? pool_size : 1000)
{}

ParticleAllocator::ParticleAllocator(const Settings & s)
	: ParticleAllocator(Estimate(s))
{}

ParticleAllocator::~ParticleAllocator()
{
	for (auto p : pools) {
		free(p);
	}
}

__forceinline Particle *ParticleAllocator::Create()
{
	if (!free_spots.size())
		AddPool();
	// Use any free spot from the list
	Particle * res = free_spots.back();
	free_spots.pop_back();
	memset(res, 0, sizeof(Particle));
	return res;
}

__forceinline void ParticleAllocator::Kill(Particle *p)
{
	free_spots.push_back(p);
}

int ParticleAllocator::Estimate(const Settings & s)
{
	// Calculate how many concurrent particles at the maximum system will have on average
	float avg_emit_rate = 1 / ((s.emission_delay_max + s.emission_delay_min) / 2);
	float avg_ttl = (s.ttl_max + s.ttl_min) / 2;
	// 1.2 is an arbitrary constant to have a little wiggle room in system goes over the average
	return static_cast<int>((avg_emit_rate * avg_ttl) * 1.2);
}

void ParticleAllocator::AddPool()
{
	Particle * pool = reinterpret_cast<Particle *>(malloc(pool_size * sizeof(Particle)));
	pools.push_back(pool);
	// The enterity of the new pool goes into free spots vector
	free_spots.reserve(free_spots.capacity() + pool_size);
	Particle * end = pool + pool_size;
	for (Particle * p = pool; p < end; p++) {
		free_spots.push_back(p);
	}
}

#pragma endregion

#pragma region "System: Particle system implementing an intrusive linked list"

System::System(vec3 system_pos, const Settings & s) :
	ISystem(system_pos, s),
	first(nullptr),
	allocator(s)
{

}

IParticle * System::Create()
{
	Particle * new_particle = CreateInternal();
	// Keeping the list linked
	new_particle->next = first;
	// Inserting new one at the start of the list
	if (first)
		first->prev = new_particle;
	first = new_particle;
	count++;
	return first;
}

IParticle * System::Kill(IParticle * p)
{
	if (!p)
		return nullptr;
	Particle * list_p = static_cast<Particle *>(p);
	Particle * next_p = list_p->next;
	Particle * prev_p = list_p->prev;
	// Updating back links from the previous and the next neighbours
	if (next_p)
		next_p->prev = prev_p;
	if (prev_p)
		prev_p->next = next_p;
	else // No previous particle means it's first in the list
		first = next_p;
	KillInternal(list_p);
	count--;
	return next_p;
}

IParticle * System::GetFirst()
{
	return first;
}

IParticle * System::GetEnd()
{
	return nullptr;
}

IParticle * System::GetNext(IParticle * p)
{
	if (p)
		return static_cast<Particle *>(p)->next;
	else
		return nullptr;
}

#ifdef __POOL_ALLOCATOR__

__forceinline Particle *System::CreateInternal()
{
	return allocator.Create();
}

__forceinline void System::KillInternal(Particle *p)
{
	allocator.Kill(p);
}

#else

__forceinline Particle *System::CreateInternal()
{
	return new Particle();	
}

__forceinline void System::KillInternal(Particle *p)
{
	delete p;
}

#endif

#pragma endregion

#pragma region "StdSystem: Particle system based on std::list implementation"

StdSystem::StdSystem(vec3 system_pos, const Settings & s) :
	ISystem(system_pos, s)
{}

ListIt StdSystem::Create()
{
	list.emplace_front();
	count++;
	return list.begin();
}

ListIt StdSystem::Kill(ListIt p)
{
	count--;
	return list.erase(p);
}

ListIt StdSystem::GetFirst()
{
	return list.begin();
}

ListIt StdSystem::GetEnd()
{
	return list.end();
}

ListIt StdSystem::GetNext(ListIt p)
{
	p++;
	return p;
}
#pragma endregion