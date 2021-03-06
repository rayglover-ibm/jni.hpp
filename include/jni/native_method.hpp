#pragma once

#include <jni/types.hpp>
#include <jni/errors.hpp>
#include <jni/functions.hpp>
#include <jni/tagging.hpp>
#include <jni/class.hpp>
#include <jni/object.hpp>

#include <exception>
#include <type_traits>

#include <iostream>

namespace jni
   {
    template < class M, class Enable = void >
    struct NativeMethodTraits;

    template < class R, class... Args >
    struct NativeMethodTraits< R (Args...) >
       {
        using Type = R (Args...);
        using ResultType = R;
       };

    template < class R, class... Args >
    struct NativeMethodTraits< R (*)(Args...) >
        : NativeMethodTraits< R (Args...) > {};

    template < class T, class R, class... Args >
    struct NativeMethodTraits< R (T::*)(Args...) const >
        : NativeMethodTraits< R (Args...) > {};

    template < class T, class R, class... Args >
    struct NativeMethodTraits< R (T::*)(Args...) >
        : NativeMethodTraits< R (Args...) > {};

    template < class M >
    struct NativeMethodTraits< M, std::enable_if_t< std::is_class<M>::value > >
        : NativeMethodTraits< decltype(&M::operator()) > {};


    /// Low-level, lambda

    namespace detail
       {
        template < class M, class T = typename NativeMethodTraits<M>::Type >
        struct FnStore { using Type = T; static T* ptr; };

        template < class M, class T >
        T* FnStore<M, T>::ptr = nullptr;
       }

    template < class M >
    auto MakeNativeMethod(const char* name, const char* sig, const M& m,
                          std::enable_if_t< std::is_class<M>::value >* = 0)
       {
        using ResultType = typename NativeMethodTraits<M>::ResultType;
        using Function = detail::FnStore<M>;

        Function::ptr = m;

        auto wrapper = [] (JNIEnv* env, auto... args)
           {
            try
               {
                return Function::ptr(env, args...);
               }
            catch (...)
               {
                ThrowJavaError(*env, std::current_exception());
                return ResultType();
               }
           };

        return JNINativeMethod< typename Function::Type > { name, sig, wrapper };
       }


    /// Low-level, function pointer

    template < class M, M method >
    auto MakeNativeMethod(const char* name, const char* sig)
       {
        using FunctionType = typename NativeMethodTraits<M>::Type;
        using ResultType = typename NativeMethodTraits<M>::ResultType;

        auto wrapper = [] (JNIEnv* env, auto... args)
           {
            try
               {
                return method(env, args...);
               }
            catch (...)
               {
                ThrowJavaError(*env, std::current_exception());
                return ResultType();
               }
           };

        return JNINativeMethod< FunctionType > { name, sig, wrapper };
       }


    /// High-level, lambda

    template < class T, T*... >
    struct NativeMethodMaker;

    template < class T, class R, class Subject, class... Args >
    struct NativeMethodMaker< R (T::*)(JNIEnv&, Subject, Args...) const >
       {
        template < class M >
        auto operator()(const char* name, const M& m)
           {
            static M method(m);

            auto wrapper = [] (JNIEnv* env, UntaggedType<Subject> subject, UntaggedType<Args>... args) -> UntaggedType<R>
               {
                return method(*env, Tag<Subject>(*subject), Tag<Args>(args)...);
               };

            return MakeNativeMethod(name, TypeSignature<R (Args...)>()(), wrapper);
           }
       };

    template < class M >
    auto MakeNativeMethod(const char* name, const M& m)
       {
        return NativeMethodMaker<decltype(&M::operator())>()(name, m);
       }


    /// High-level, function pointer

    template < class R, class Subject, class... Args, R (*method)(JNIEnv&, Subject, Args...) >
    struct NativeMethodMaker< R (JNIEnv&, Subject, Args...), method >
       {
        auto operator()(const char* name)
           {
            auto wrapper = [] (JNIEnv* env, UntaggedType<Subject> subject, UntaggedType<Args>... args) -> UntaggedType<R>
               {
                return method(*env, Tag<Subject>(*subject), Tag<Args>(args)...);
               };

            return MakeNativeMethod(name, TypeSignature<R (Args...)>()(), wrapper);
           }
       };

    template < class M, M method >
    auto MakeNativeMethod(const char* name)
       {
        using FunctionType = typename NativeMethodTraits<M>::Type;
        return NativeMethodMaker<FunctionType, method>()(name);
       }


    /// High-level peer, lambda

    template < class L, class >
    class NativePeerLambdaMethod;

    template < class L, class R, class P, class... Args >
    class NativePeerLambdaMethod< L, R (L::*)(JNIEnv&, P&, Args...) const >
       {
        private:
            const char* name;
            L lambda;

        public:
            NativePeerLambdaMethod(const char* n, const L& l)
               : name(n), lambda(l)
               {}

            template < class Peer, class TagType, class = std::enable_if_t< std::is_same<P, Peer>::value > >
            auto operator()(const Field<TagType, jlong>& field)
               {
                auto wrapper = [field, lambda = lambda] (JNIEnv& env, Object<TagType> obj, Args... args)
                   {
                    return lambda(env, *reinterpret_cast<P*>(obj.Get(env, field)), std::move(args)...);
                   };

                return MakeNativeMethod(name, wrapper);
               }
       };

    template < class L >
    auto MakeNativePeerMethod(const char* name, const L& lambda,
                              std::enable_if_t< std::is_class<L>::value >* = 0)
       {
        return NativePeerLambdaMethod<L, decltype(&L::operator())>(name, lambda);
       }


    /// High-level peer, function pointer

    template < class M, M* >
    class NativePeerFunctionPointerMethod;

    template < class R, class P, class... Args, R (*method)(JNIEnv&, P&, Args...) >
    class NativePeerFunctionPointerMethod< R (JNIEnv&, P&, Args...), method >
       {
        private:
            const char* name;

        public:
            NativePeerFunctionPointerMethod(const char* n)
               : name(n)
               {}

            template < class Peer, class TagType, class = std::enable_if_t< std::is_same<P, Peer>::value > >
            auto operator()(const Field<TagType, jlong>& field)
               {
                auto wrapper = [field] (JNIEnv& env, Object<TagType> obj, Args... args)
                   {
                    return method(env, *reinterpret_cast<P*>(obj.Get(env, field)), std::move(args)...);
                   };

                return MakeNativeMethod(name, wrapper);
               }
       };

    template < class M, M method >
    auto MakeNativePeerMethod(const char* name,
                              std::enable_if_t< !std::is_member_function_pointer<M>::value >* = 0)
       {
        using FunctionType = typename NativeMethodTraits<M>::Type;
        return NativePeerFunctionPointerMethod<FunctionType, method>(name);
       }


    /// High-level peer, member function pointer

    template < class M, M >
    class NativePeerMemberFunctionMethod;

    template < class R, class P, class... Args, R (P::*method)(JNIEnv&, Args...) >
    class NativePeerMemberFunctionMethod< R (P::*)(JNIEnv&, Args...), method >
       {
        private:
            const char* name;

        public:
            NativePeerMemberFunctionMethod(const char* n)
               : name(n)
               {}

            template < class Peer, class TagType, class = std::enable_if_t< std::is_same<P, Peer>::value > >
            auto operator()(const Field<TagType, jlong>& field)
               {
                auto wrapper = [field] (JNIEnv& env, Object<TagType> obj, Args... args)
                   {
                    return (reinterpret_cast<P*>(obj.Get(env, field))->*method)(env, std::move(args)...);
                   };

                return MakeNativeMethod(name, wrapper);
               }
       };

    template < class M, M method >
    auto MakeNativePeerMethod(const char* name,
                              std::enable_if_t< std::is_member_function_pointer<M>::value >* = 0)
       {
        return NativePeerMemberFunctionMethod<M, method>(name);
       }


    /**
     * A registration function for native methods on a "native peer": a long-lived native
     * object corresponding to a Java object, usually created when the Java object is created
     * and destroyed when the Java object's finalizer runs.
     *
     * It assumes that the Java object has a field, named by `fieldName`, of Java type `long`,
     * which is used to hold a pointer to the native peer.
     *
     * `Methods` must be a sequence of `NativePeerMethod` instances, instantiated with pointer
     * to member functions of the native peer class. For each method in `methods`, a native
     * method is bound with a signature corresponding to that of the member function. The
     * wrapper for that native method obtains the native peer instance from the Java field and
     * calls the native peer method on it, passing along any arguments.
     *
     * An overload is provided that accepts a Callable object with a unique_ptr result type and
     * the names for native creation and finalization methods, allowing creation and disposal of
     * the native peer from Java.
     *
     * For an example of all of the above, see the `examples` directory.
     */

    template < class Peer, class TagType, class... Methods >
    void RegisterNativePeer(JNIEnv& env, const Class<TagType>& clazz, const char* fieldName, Methods&&... methods)
       {
        static Field<TagType, jni::jlong> field { env, clazz, fieldName };

#if defined(_MSC_VER) && _MSC_VER <= 1900 && !defined(__c2__) // VS2015
        RegisterNatives(env, clazz, methods.operator()<Peer>(field)...);
#else
        RegisterNatives(env, clazz, methods.template operator()<Peer>(field)...);
#endif
       }

    template < class Peer, class TagType, class >
    struct NativePeerHelper;

    template < class Peer, class TagType, class... Args >
    struct NativePeerHelper< Peer, TagType, std::unique_ptr<Peer> (JNIEnv&, Args...) >
       {
        using UniquePeer = std::unique_ptr<Peer>;
        using Initializer = UniquePeer (JNIEnv&, Args...);

        auto MakeInitializer(const Field<TagType, jlong>& field, const char* name, Initializer* initializer) const
           {
            auto wrapper = [field, initializer] (JNIEnv& e, Object<TagType> obj, std::decay_t<Args>... args)
               {
                UniquePeer previous(reinterpret_cast<Peer*>(obj.Get(e, field)));
                UniquePeer instance(initializer(e, std::move(args)...));
                obj.Set(e, field, reinterpret_cast<jlong>(instance.get()));
                instance.release();
               };

            return MakeNativeMethod(name, wrapper);
           }

        auto MakeFinalizer(const Field<TagType, jlong>& field, const char* name) const
           {
            auto wrapper = [field] (JNIEnv& e, Object<TagType> obj)
               {
                UniquePeer instance(reinterpret_cast<Peer*>(obj.Get(e, field)));
                if (instance) obj.Set(e, field, jlong(0));
                instance.reset();
               };

            return MakeNativeMethod(name, wrapper);
           }
       };

    template < class Peer, class TagType, class Initializer, class... Methods >
    void RegisterNativePeer(JNIEnv& env, const Class<TagType>& clazz, const char* fieldName,
                            Initializer initialize,
                            const char* initializeMethodName,
                            const char* finalizeMethodName,
                            Methods&&... methods)
       {
        static Field<TagType, jlong> field { env, clazz, fieldName };

        using InitializerMethodType = typename NativeMethodTraits<Initializer>::Type;
        NativePeerHelper<Peer, TagType, InitializerMethodType> helper;

#if defined(_MSC_VER) && _MSC_VER <= 1900 && !defined(__c2__) // VS2015
        RegisterNatives(env, clazz,
            helper.MakeInitializer(field, initializeMethodName, initialize),
            helper.MakeFinalizer(field, finalizeMethodName),
            methods.operator()<Peer>(field)...);
#else
        RegisterNatives(env, clazz,
            helper.MakeInitializer(field, initializeMethodName, initialize),
            helper.MakeFinalizer(field, finalizeMethodName),
            methods.template operator()<Peer>(field)...);
#endif
       }
   }
