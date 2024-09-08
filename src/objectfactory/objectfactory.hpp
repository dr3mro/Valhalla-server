#pragma once

/// A factory class that manages a pool of objects of a specific type.

class ObjectFactory
{
   public:
    explicit ObjectFactory();
    virtual ~ObjectFactory() = default;
};
