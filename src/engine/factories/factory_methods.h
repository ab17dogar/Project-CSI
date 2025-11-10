#ifndef FACTORY_METHODS_H
#define FACTORY_METHODS_H

#include <memory> // for std::shared_ptr

#include <string>

class world;

std::shared_ptr<world> LoadScene(std::string fileName);


#endif
