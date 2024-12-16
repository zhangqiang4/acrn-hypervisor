class HookManager:
    def __init__(self, clazz):
        self.clazz = clazz
        self.manage = {}
        self.Me = object

    def register(self, hooked_class):
        self.manage.update({hooked_class.__name__: hooked_class})

    def before_hook(self):
        self.Me = self.clazz

    def after_hook(self):
        self.clazz = self.Me

    def process_hook(self, mapping):
        class Proxy(self.Me):
            self.mapping = mapping
            for k, v in self.mapping.items():
                setattr(self.Me, k, v)

    def hook(self, mapping):
        self.mapping = mapping
        self.before_hook()
        self.process_hook(self.mapping)
        self.after_hook()
